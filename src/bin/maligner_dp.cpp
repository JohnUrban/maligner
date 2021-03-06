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

// common includes
#include "timer.h"
#include "common_defs.h"

using std::string;
using std::unordered_map;
using std::ostringstream;
using std::cerr;
using std::cout;
using std::ofstream;
// using namespace std;


#define PACKAGE_NAME "maligner dp"
#include "maligner_dp_includes.h"


// Wrapper around all of the Map structures we need to store
// in order to perform DP alignments.
// using namespace kmer_match;
using namespace maligner_dp;
using namespace maligner_maps;
using maligner_dp::Alignment;


using lmm_utils::Timer;

typedef ScoreMatrix<row_order_tag> ScoreMatrixType;
typedef AlignTask<ScoreMatrixType, Chi2SizingPenalty> AlignTaskType;

typedef unordered_map<string, RefMapWrapper> RefMapDB;


/////////////////////////////////////////////////////////////////
// Generate permuted maps by concatenating all fragments
RefMapDB generate_permuted_maps(RefMapDB& ref_map_db, size_t n) {
  
  // Gather all fragments from the reference map.
  FragVec all_frags;

  for(RefMapDB::const_iterator iter = ref_map_db.begin(); iter != ref_map_db.end(); iter++) {
    const RefMapWrapper& ref_map_wrapper = iter->second;
    const FragVec& frags = ref_map_wrapper.get_frags_noncircularized();
    std::cerr << "Map: " << iter->first << " num_frags: " << frags.size() << std::endl;
    all_frags.insert(all_frags.end(), frags.begin(), frags.end());
  }

  std::cerr << "Have " << all_frags.size() << " reference fragments." << std::endl;

  // Compute the total sum of the frags
  int size = 0;
  for(auto frag : all_frags) {
    size += frag;
  }

  const bool is_circular = opt::reference_is_circular;
  const bool is_bounded = false;
  const bool is_random = true;

  RefMapDB ret;

  for(size_t i = 0; i < n; i++) {

    ostringstream map_name;
    map_name << "random_map_" << i;

    std::string map_name_str = map_name.str();
    std::cerr << "map_name: " << map_name_str << std::endl;

    FragVec random_frags = permute(all_frags);

    std::cerr << "have " << random_frags.size() << " random frags." << std::endl;

    Map random_map(map_name_str, size, random_frags);
    MapData random_map_data(map_name_str, random_frags.size(), size,
      is_circular, is_bounded, is_random);

    RefMapWrapper random_map_wrapper(random_map, random_map_data,
        maligner_dp::opt::ref_max_misses,
        maligner_dp::opt::sd_rate,
        maligner_dp::opt::min_sd
    );

    ret.insert(RefMapDB::value_type(map_name_str, random_map_wrapper));

  }

  return ret;

}


///////////////////////////////////////////////////////////////////////////////////////////
// Align the query to each map, and return a vector of the best random alignments
AlignmentVec run_permutation_test(RefMapDB& permuted_map_db, const QueryMapWrapper& qmw,
  ScoreMatrixType& sm, AlignOpts& align_opts) {



  Timer timer;
  timer.start();
  std::cerr << "Running permutation test... ";

  AlignmentVec alignments;
  alignments.reserve(permuted_map_db.size());


  for(auto permuted_map_iter = permuted_map_db.begin();
        permuted_map_iter != permuted_map_db.end();
        permuted_map_iter++) {


      const RefMapWrapper& rmw = permuted_map_iter->second;

      AlignTaskType task_forward(
        const_cast<MapData*>(&qmw.map_data_),
        const_cast<MapData*>(&rmw.map_data_),
        &qmw.get_frags(),
        &rmw.get_frags(), 
        &qmw.get_partial_sums_forward(),
        &rmw.get_partial_sums(),
        &rmw.sd_inv_,
        &qmw.ix_to_locs_,
        &rmw.ix_to_locs_,
        0, // ref_offset
        &sm,
        nullptr,
        true, // query_is_forward
        true, // ref_is_forward
        align_opts
      );

      AlignTaskType task_reverse(
        const_cast<MapData*>(&qmw.map_data_),
        const_cast<MapData*>(&rmw.map_data_),
        &qmw.get_frags_reverse(),
        &rmw.get_frags(), 
        &qmw.get_partial_sums_reverse(),
        &rmw.get_partial_sums(),
        &rmw.sd_inv_,
        &qmw.ix_to_locs_,
        &rmw.ix_to_locs_,        
        0, // ref_offset
        &sm,
        nullptr,
        true, // query_is_forward
        false, // ref_is_forward
        align_opts
      );

      // print_align_task(std::cerr, task_forward);
      Alignment forward_aln = make_best_alignment_using_partials(task_forward);

      // print_align_task(std::cerr, task_reverse);
      Alignment reverse_aln = make_best_alignment_using_partials(task_reverse);

      if(forward_aln.total_rescaled_score > reverse_aln.total_rescaled_score) {
        alignments.push_back(std::move(forward_aln));
      } else {
        alignments.push_back(std::move(reverse_aln));
      }


    }

    std::sort(alignments.begin(), alignments.end(), AlignmentRescaledScoreComp());

    std::cerr << timer << "\n";

    return alignments;
}

void assign_pval(const AlignmentVec& sorted_random_alns, Alignment& aln) {

  // Compute how many sorted_random_alns have a score equal to or lower than aln.
  AlignmentVec::const_iterator ubound = std::upper_bound(
    sorted_random_alns.begin(),
    sorted_random_alns.end(),
    aln,
    AlignmentRescaledScoreComp());

  // ubound is the first alignment in sorted_random_alns with a total_rescaled_score
  // worse than aln.
  int num_better = ubound - sorted_random_alns.begin();

  std::cerr << "num_random_alns: " << sorted_random_alns.size() <<
  " first score: " << sorted_random_alns[0].total_rescaled_score <<
  " last score: " << sorted_random_alns.back().total_rescaled_score <<
  " aln score: " << aln.total_rescaled_score <<
  " num better: " << num_better << std::endl;

  aln.p_val =  double(num_better)/sorted_random_alns.size();

  return;
}


int main(int argc, char* argv[]) {

  using maligner_dp::Alignment;
  maligner_dp::opt::program_name = argv[0];
  parse_args(argc, argv);

  print_args(std::cerr);

  ofstream score_file;

  if(!maligner_dp::opt::score_file.empty()) {
    score_file.open(maligner_dp::opt::score_file); 
  }


  Timer timer;

  AlignOpts align_opts(maligner_dp::opt::query_miss_penalty,
                       maligner_dp::opt::ref_miss_penalty,
                       maligner_dp::opt::query_max_misses,
                       maligner_dp::opt::ref_max_misses,
                       maligner_dp::opt::sd_rate,
                       maligner_dp::opt::min_sd,
                       maligner_dp::opt::max_chunk_sizing_error,
                       maligner_dp::opt::ref_max_miss_rate,
                       maligner_dp::opt::query_max_miss_rate,
                       maligner_dp::opt::alignments_per_reference,
                       maligner_dp::opt::min_alignment_spacing,
                       maligner_dp::opt::neighbor_delta,
                       maligner_dp::opt::query_is_bounded, // Perhaps this should be part of the MapData instead of AlignOpts
                       maligner_dp::opt::ref_is_bounded, // Perhaps this should be part of the MapData instead of AlignOpts
                       maligner_dp::opt::query_rescaling,
                       maligner_dp::opt::min_query_scaling,
                       maligner_dp::opt::max_query_scaling);

  // Build a database of reference maps. 
  MapVec ref_maps(read_maps(maligner_dp::opt::ref_maps_file));
  cerr << "Read " << ref_maps.size() << " reference maps.\n";

  // Store reference maps in an unordered map.
  RefMapDB ref_map_db;
  for(auto i = ref_maps.begin(); i != ref_maps.end(); i++) {
    ref_map_db.insert( RefMapDB::value_type(i->name_,
        RefMapWrapper(*i, maligner_dp::opt::reference_is_circular, 
                          maligner_dp::opt::ref_max_misses,
                          maligner_dp::opt::sd_rate,
                          maligner_dp::opt::min_sd)) );
  }

 cerr << "Wrapped " << ref_map_db.size() << " reference maps.\n";


 // Generated permuted reference map for permutation test, if necessary
 RefMapDB permuted_map_db = generate_permuted_maps(ref_map_db, opt::num_permutation_trials);

 // Generate a single ScoreMatrix to use throughout this program.
 ScoreMatrixType sm;


 MapReader query_map_reader(maligner_dp::opt::query_maps_file);
 Map query_map;
 AlignmentVec alns_forward, alns_reverse, all_alignments;


 std::cout << AlignmentHeader();

 while(query_map_reader.next(query_map)) {

    all_alignments.clear();
    alns_forward.clear();
    alns_reverse.clear();

    const IntVec& query_frags_forward = query_map.frags_;

    if(query_map.frags_.size() < maligner_dp::opt::min_query_frags) {

      if(opt::verbose) {
        std::cerr << "Skipping map " << query_map.name_ << " with " 
                  << query_map.frags_.size() << " fragments.\n";
      }

      continue;
    }

    if(query_map.frags_.size() > maligner_dp::opt::max_query_frags) {

      if(opt::verbose) {
        std::cerr << "Skipping map " << query_map.name_ << " with " 
                  << query_map.frags_.size() << " fragments.\n";
      }

      continue;
    }

    const QueryMapWrapper qmw(query_map, align_opts.query_max_misses);

    const size_t num_query_frags = query_map.frags_.size();

    Timer query_timer;
    query_timer.start();

    for(auto ref_map_iter = ref_map_db.begin();
        ref_map_iter != ref_map_db.end();
        ref_map_iter++) {

      using maligner_vd::ScoreMatrixProfile;

      const RefMapWrapper& rmw = ref_map_iter->second;

      // const IntVec* p_frags_forward = &query_frags_forward;
      // const IntVec* p_frags_reverse = &query_frags_reverse;

      AlignTaskType task_forward(
        const_cast<MapData*>(&qmw.map_data_),
        const_cast<MapData*>(&rmw.map_data_),
        &qmw.get_frags(),
        &rmw.get_frags(), 
        &qmw.get_partial_sums_forward(),
        &rmw.get_partial_sums(),
        &rmw.sd_inv_,
        &qmw.ix_to_locs_,
        &rmw.ix_to_locs_,
        0, // ref_offset
        &sm,
        &all_alignments,
        true, // query_is_forward
        true, // ref_is_forward
        align_opts
      );

      AlignTaskType task_reverse(
        const_cast<MapData*>(&qmw.map_data_),
        const_cast<MapData*>(&rmw.map_data_),
        &qmw.get_frags_reverse(),
        &rmw.get_frags(), 
        &qmw.get_partial_sums_reverse(),
        &rmw.get_partial_sums(),
        &rmw.sd_inv_,
        &qmw.ix_to_locs_,
        &rmw.ix_to_locs_,        
        0, // ref_offset
        &sm,
        &all_alignments,
        false, // query_is_forward
        true, // ref_is_forward
        align_opts
      );

      // ScoreMatrixProfile profile_forward, profile_rev;

      // std::cerr << "Align task forward: "; print_align_task(std::cerr, task_forward);
      // std::cerr << "Align task reverse: "; print_align_task(std::cerr, task_reverse);

      // std::cerr << "Aligning " << query_map.name_ << " to " << rmw.map_.name_ << "\n";
      Timer timer;

      // Align Forward
      {

        timer.start();
        // Alignment aln_forward = make_best_alignment_using_partials(task_forward);
        int num_alignments = make_best_alignments_using_partials(task_forward);
        timer.end();

        if(opt::verbose) {
          std::cerr << "Num alignments forward: " << num_alignments
                    << " " << timer << "\n";
        }

        ////////////////////////////////////////////////
        // DEBUG      
        // std::cerr << sm.getNumRows() << " x " << sm.getNumCols() << "\n";    
        // std::cerr << "last_row: " << sm.countFilledByRow(sm.getNumRows() - 1) << "\n";
        // profile_forward = get_score_matrix_row_profile(sm,
        //   sm.getNumRows()-1,
        //   qmw.get_name(),
        //   rmw.get_name(), maligner_vd::AlignmentOrientation::RF_QF);
        
        // std::sort(profile_forward.begin(), profile_forward.end(), maligner_vd::ScoreMatrixRecordScoreCmp());
        
        // if(profile_forward.size() > 100) {
        //   profile_forward.resize(100);
        // }
        //////////////////////////////////////////////

      }

      // Align Reverse
      {

        timer.start();
        int num_alignments = make_best_alignments_using_partials(task_reverse);
        timer.end();

        if(opt::verbose) {
          std::cerr << "Num alignments reverse: " << num_alignments
                    << " " << timer << "\n";
        }

        ////////////////////////////////////////////////////////////////////
        // DEBUG
        // std::cerr << sm.getNumRows() << " x " << sm.getNumCols() << "\n";
        // std::cerr << "last_row: " << sm.countFilledByRow(sm.getNumRows() - 1) << "\n";
        // profile_rev = get_score_matrix_row_profile(sm,
        //   sm.getNumRows()-1,
        //   qmw.get_name(),
        //   rmw.get_name(), maligner_vd::AlignmentOrientation::RF_QR);
        
        // std::sort(profile_rev.begin(), profile_rev.end(), maligner_vd::ScoreMatrixRecordScoreCmp());

        // if(profile_rev.size() > 100) {
        //   profile_rev.resize(100);
        // }
        ////////////////////////////////////////////////////////////////////

      }


      ////////////////////////////////////////////////////////////////////
      // DEBUG WRITE OUT!
      // std::cerr << maligner_vd::ScoreMatrixRecordHeader() << "\n";
      // for(auto& rec : profile_forward) {
      //   std::cerr << rec << "\n";
      // }
      // for(auto& rec : profile_rev) {
      //   std::cerr << rec << "\n";
      // }
      ////////////////////////////////////////////////////////////////////

    }

    // Sort alignments by the rescaled scores.
    std::sort(all_alignments.begin(), all_alignments.end(), AlignmentRescaledScoreComp());

    const size_t num_all_alignments = all_alignments.size();

    // Compute the mad score for the alignments.
    double mad = compute_mad_scores(all_alignments, maligner_dp::opt::max_alignments_mad, maligner_dp::opt::min_mad);

    const int max_ind = std::min(int(all_alignments.size()), opt::max_alignments);
    
    query_timer.end();    

    std::cerr << "done aligning query: " << query_map.name_ << " "
              << "aln_num: " << num_all_alignments << " "
              << "mad: " << mad << "\n"
              << query_timer << "\n";


    //////////////////////////////////////////////////           
    // Run permutation test to assign bootstrapped p-values, if necessary

    if(opt::num_permutation_trials > 0) {

      query_timer.start();
      std::cerr << "Runing permutation test...";


      // Null distribution of alignment scores
      AlignmentVec random_alns = run_permutation_test(permuted_map_db, qmw, sm, align_opts);

      // Assign pvals
      const size_t n = all_alignments.size();
      for(int i = 0; i < n; i++) {
      // for(Alignment& aln : all_alignments) {
        Alignment& aln = all_alignments[i];
        assign_pval(random_alns, aln);
      }

      query_timer.end();
      std::cerr << "done running permutation tests. " << query_timer;

    }

    ////////////////////////////////////////////////////
    // Print alignments if they pass the filter.
    for(int i = 0; i < max_ind; i++) {

      Alignment& aln = all_alignments[i];

      if(aln.score_per_inner_chunk < opt::max_score_per_inner_chunk) {
        print_alignment(std::cout, all_alignments[i]);
      }

    }

    if(score_file.is_open()) {

      for(const auto& aln : all_alignments) {
        score_file << AlignmentScoreInfo(aln) << "\n";
      }

    }

    std::cerr << "*****************************************\n";

 }

  if(score_file.is_open())
    score_file.close();

  std::cerr << "maligner_dp done.\n";
  return EXIT_SUCCESS;

}
