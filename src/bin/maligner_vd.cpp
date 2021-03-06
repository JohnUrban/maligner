#include <iostream>
#include <algorithm>
#include <vector>
#include <string>
#include <unordered_map>
#include <sstream>
#include <fstream>
#include <chrono>
#include <getopt.h>

// kmer_match includes
#include "map.h"
#include "map_reader.h"
#include "map_frag.h"
#include "map_frag_db.h"
#include "error_model.h"
#include "map_chunk.h"
#include "ref_alignment.h"
#include "map_chunk_db.h"
#include "map_sampler.h"

// dp includes  
#include "map_data.h"
#include "align.h"
#include "utils.h"
#include "ScoreMatrix.h"

// vd includes
#include "score_matrix_vd.h"
#include "score_matrix_vd_db.h"
#include "score_matrix_profile.h"

// common includes
#include "timer.h"
#include "common_defs.h"

using std::string;
using std::unordered_map;
using std::cerr;
using std::cout;
// using namespace std;


#define PACKAGE_NAME "maligner vd"
#include "maligner_vd_includes.h"



// Wrapper around all of the Map structures we need to store
// in order to perform DP alignments.
// using namespace kmer_match;
using namespace maligner_vd;
using namespace maligner_dp;
using namespace maligner_maps;
using maligner_dp::Alignment;
using maligner_dp::AlignmentHeader;
using maligner_dp::ScoreMatrix;
using maligner_dp::row_order_tag;

using lmm_utils::Timer;

typedef ScoreMatrix<row_order_tag> ScoreMatrixType;
typedef AlignTask<ScoreMatrixType, Chi2SizingPenalty> AlignTaskType;
typedef maligner_vd::RefScoreMatrixVD<ScoreMatrixType> RefScoreMatrixVDType;
typedef maligner_vd::RefScoreMatrixVDDB<RefScoreMatrixVDType> RefScoreMatrixDB;
typedef std::vector<RefScoreMatrixVDType> RefScoreMatrixVDVec;

int main(int argc, char* argv[]) {

  using maligner_dp::Alignment;
  maligner_vd::opt::program_name = argv[0];
  parse_args(argc, argv);

  print_args(std::cerr);

  Timer timer;

  AlignOpts align_opts(maligner_vd::opt::query_miss_penalty,
                       maligner_vd::opt::ref_miss_penalty,
                       maligner_vd::opt::query_max_misses,
                       maligner_vd::opt::ref_max_misses,
                       maligner_vd::opt::sd_rate,
                       maligner_vd::opt::min_sd,
                       maligner_vd::opt::max_chunk_sizing_error,
                       maligner_vd::opt::ref_max_miss_rate,
                       maligner_vd::opt::query_max_miss_rate,
                       maligner_vd::opt::alignments_per_reference,
                       maligner_vd::opt::min_alignment_spacing,
                       maligner_vd::opt::neighbor_delta,
                       maligner_vd::opt::query_is_bounded, // Perhaps this should be part of the MapData instead of AlignOpts
                       maligner_vd::opt::ref_is_bounded, // Perhaps this should be part of the MapData instead of AlignOpts
                       maligner_vd::opt::query_rescaling,
                       maligner_vd::opt::min_query_scaling,
                       maligner_vd::opt::max_query_scaling);

  // Build a database of reference maps. 
  MapVec ref_maps(read_maps(maligner_vd::opt::ref_maps_file));
  cerr << "Read " << ref_maps.size() << " reference maps.\n";

  RefScoreMatrixVDVec ref_score_matrix_vd_vec;
  RefScoreMatrixDB ref_score_matrix_db;
  for(auto& rm : ref_maps ) {

    RefMapWrapper rmw(rm, maligner_vd::opt::reference_is_circular, 
                          maligner_vd::opt::ref_max_misses,
                          maligner_vd::opt::sd_rate,
                          maligner_vd::opt::min_sd);

    ref_score_matrix_db.add_ref_map(std::move(rmw));

  }

  cerr << "Wrapped " << ref_score_matrix_db.size() << " reference maps.\n";

  MapReader query_map_reader(maligner_vd::opt::query_maps_file);
  Map query_map;
  AlignmentVec alns_forward, alns_reverse, all_alignments;

  ////////////////////////////////////////////////////////
  // Open output files for the different alignment types.
  // std::ofstream fout_rf_qf(maligner_vd::opt::output_pfx + ".rf_qf.aln");
  // std::ofstream fout_rf_qr(maligner_vd::opt::output_pfx + ".rf_qr.aln");
  // std::ofstream fout_rr_qf(maligner_vd::opt::output_pfx + ".rr_qf.aln");
  // std::ofstream fout_rr_qr(maligner_vd::opt::output_pfx + ".rr_qr.aln");
  std::ofstream fout_prefix(maligner_vd::opt::output_pfx + ".pfx.aln");
  std::ofstream fout_suffix(maligner_vd::opt::output_pfx + ".sfx.aln");
  std::ofstream fout_full_aln(maligner_vd::opt::output_pfx + ".full.aln");

  // fout_rf_qf << AlignmentHeader();
  // fout_rf_qr << AlignmentHeader();
  // fout_rr_qf << AlignmentHeader();
  // fout_rr_qr << AlignmentHeader();
  fout_prefix << AlignmentHeader();
  fout_suffix << AlignmentHeader();
  fout_full_aln << AlignmentHeader();

  std::cout << maligner_vd::ScoreMatrixRecordHeader() << "\n";

  while(query_map_reader.next(query_map)) {

    all_alignments.clear();
    alns_forward.clear();
    alns_reverse.clear();

    const IntVec& query_frags_forward = query_map.frags_;

    if(query_map.frags_.size() < maligner_vd::opt::min_query_frags) {

      if(maligner_vd::opt::verbose) {
        std::cerr << "Skipping map " << query_map.name_ << " with " 
                  << query_map.frags_.size() << " fragments.\n";
      }

      continue;
    }

    if(query_map.frags_.size() > maligner_vd::opt::max_query_frags) {

      if(maligner_vd::opt::verbose) {
        std::cerr << "Skipping map " << query_map.name_ << " with " 
                  << query_map.frags_.size() << " fragments.\n";
      }

      continue;
    }

    const QueryMapWrapper qmw(query_map, align_opts.query_max_misses);
    const size_t num_query_frags = query_map.frags_.size();

    Timer query_timer;
    query_timer.start();

    ref_score_matrix_db.aln_to_forward_refs(qmw, align_opts);
    ref_score_matrix_db.aln_to_reverse_refs(qmw, align_opts);


    ref_score_matrix_db.compute_query_prefix_mscores(maligner_vd::opt::max_alignments_mad, maligner_vd::opt::min_mad, qmw.get_name());
    ref_score_matrix_db.compute_query_suffix_mscores(maligner_vd::opt::max_alignments_mad, maligner_vd::opt::min_mad, qmw.get_name());

    //////////////////////////////////////////////////////////////////////////////////////
    // Print alignments
    int wrote_count = 0;

    // {
    //   AlignmentVec alns = ref_score_matrix_db.get_best_alignments_rf_qf(maligner_vd::opt::max_alignments, maligner_vd::opt::min_aln_chunks);
    //   // std::cerr << "RFQF: " << alns.size() << " alns.\n";
    //   // std::cerr << "max_m_score: " << maligner_vd::opt::max_m_score << "\n";
    //   for(const auto& a : alns) {
    //     // std::cerr << "aln m_score: " << a.m_score 
    //     //           << " total_score: " << a.total_score << "\n";

    //     if(a.m_score <= maligner_vd::opt::max_m_score) {
    //       print_alignment(fout_rf_qf, a);
    //       // print_alignment(std::cerr, a);
    //       wrote_count++;
    //     }
    //   }

    // }

    // {
    //   AlignmentVec alns = ref_score_matrix_db.get_best_alignments_rf_qr(maligner_vd::opt::max_alignments, maligner_vd::opt::min_aln_chunks);
    //   // std::cerr << "RFQR: " << alns.size() << " alns.\n";
    //   for(const auto& a : alns) {
    //     // std::cerr << "aln m_score: " << a.m_score << "\n";
    //     if(a.m_score <= maligner_vd::opt::max_m_score) {
    //       print_alignment(fout_rf_qr, a);
    //       // print_alignment(std::cerr, a);
    //       wrote_count++;
    //     }
    //   }
    // }

    // {
    //   AlignmentVec alns = ref_score_matrix_db.get_best_alignments_rr_qf(maligner_vd::opt::max_alignments, maligner_vd::opt::min_aln_chunks);
    //   // std::cerr << "RRQF: " << alns.size() << " alns.\n";
    //   for(const auto& a : alns) {
    //     // std::cerr << "aln m_score: " << a.m_score << "\n";
    //     if(a.m_score >= maligner_vd::opt::max_m_score) {
    //       print_alignment(fout_rr_qf, a);
    //       // print_alignment(std::cerr, a);
    //       wrote_count++;
    //     }
    //   }
    // } 

    // {
    //   AlignmentVec alns = ref_score_matrix_db.get_best_alignments_rr_qr(maligner_vd::opt::max_alignments, maligner_vd::opt::min_aln_chunks);
    //   // std::cerr << "RRQR: " << alns.size() << " alns.\n";
    //   for(const auto& a : alns) {
    //     // std::cerr << "aln m_score: " << a.m_score << "\n";
    //     if(a.m_score <= maligner_vd::opt::max_m_score) {
    //       print_alignment(fout_rr_qr, a);
    //       // print_alignment(std::cerr, a);
    //       wrote_count++;
    //     }
    //   }
    // }

    {
      AlignmentVec alns = ref_score_matrix_db.get_best_alignments_prefix(maligner_vd::opt::max_alignments, maligner_vd::opt::min_aln_chunks);
      // std::cerr << "RRQR: " << alns.size() << " alns.\n";
      for(const auto& a : alns) {
        // std::cerr << "aln m_score: " << a.m_score << "\n";
        // if(a.m_score <= maligner_vd::opt::max_m_score) {
          print_alignment(fout_prefix, a);
          // print_alignment(std::cerr, a);
          wrote_count++;
        // }
      }
    }

    {
      AlignmentVec alns = ref_score_matrix_db.get_best_alignments_suffix(maligner_vd::opt::max_alignments, maligner_vd::opt::min_aln_chunks);
      // std::cerr << "RRQR: " << alns.size() << " alns.\n";
      for(const auto& a : alns) {
        // std::cerr << "aln m_score: " << a.m_score << "\n";
        // if(a.m_score <= maligner_vd::opt::max_m_score) {
          print_alignment(fout_suffix, a);
          // print_alignment(std::cerr, a);
          wrote_count++;
        // }
      }
    }

    {
      AlignmentVec alns = ref_score_matrix_db.get_best_full_alignments(maligner_vd::opt::max_alignments);
      // std::cerr << "RRQR: " << alns.size() << " alns.\n";
      for(const auto& a : alns) {
        // std::cerr << "aln m_score: " << a.m_score << "\n";
        if(a.m_score <= maligner_vd::opt::max_m_score) {
          print_alignment(fout_full_aln, a);
          // print_alignment(std::cerr, a);
          wrote_count++;
        }
      }
    }     

    // Output the best partial alignments.
    ///////////////////////////////////////////////////////////////////////////////////////
    std::cout << ref_score_matrix_db.get_score_matrix_profile_rf_qf(qmw.get_name())
              << ref_score_matrix_db.get_score_matrix_profile_rf_qr(qmw.get_name())
              << ref_score_matrix_db.get_score_matrix_profile_rr_qf(qmw.get_name())
              << ref_score_matrix_db.get_score_matrix_profile_rr_qr(qmw.get_name());

    std::cerr << "-------------------------------\n";

    query_timer.end();    

    std::cerr << "done aligning query: " << query_map.name_ << ". Wrote " << wrote_count << " alignments. "
              << query_timer << "\n";

    std::cerr << "*****************************************\n";
  
 }

  std::cerr << "maligner_vd done.\n";

  // fout_rf_qf.close();
  // fout_rf_qr.close();
  // fout_rr_qf.close();
  // fout_rr_qr.close();
  fout_prefix.close();
  fout_suffix.close();
  fout_full_aln.close();

  return EXIT_SUCCESS;

}
