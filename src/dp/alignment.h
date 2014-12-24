#ifndef ALIGNMENT_H
#define ALIGNMENT_H

#include <vector>

#include "matched_chunk.h"

namespace maligner_dp {

  using std::size_t; // Very confused that this need to be declared. I thought size_t was a thing.

  // Forward Declarations
  class AlignOpts;

  class Alignment {
  public:

    //Alignment(MatchedChunkVec& mc) : matched_chunks(mc) {};

    // We must provide a default constructor because
    // we have provided a specialized constructor.
    Alignment() : is_valid(false) {
    }

    Alignment(MatchedChunkVec& mc, Score& s) :
      matched_chunks(mc),
      rescaled_matched_chunks(matched_chunks),
      score(s),
      rescaled_score(s),
      query_scaling_factor(1.0)
    {
      summarize();
    }

    
    // Use the default copy constructor/assignment
    Alignment(const Alignment&) = default;
    Alignment& operator=(const Alignment&) = default;

    // Use the default move constructor/assignment
    Alignment(Alignment&&) = default;
    Alignment& operator=(Alignment&&) = default;

    // rescale the query chunks using the query_scaling_factor, and
    // recompute the sizing error for those chunks.
    void rescale_matched_chunks(const AlignOpts& align_opts);

    // Compute summary statistics from matched chunks.
    void summarize(); 

    // Reset the summary
    void reset_stats();

    // Attributes
    MatchedChunkVec matched_chunks;
    MatchedChunkVec rescaled_matched_chunks;
    Score score;
    Score rescaled_score;

    double total_score;
    double total_rescaled_score;

    // summary statistics of an alignment.
    // These are computable from the matched_chunks
    int num_matched_sites;
    int query_misses;
    int ref_misses;
    double query_miss_rate;
    double ref_miss_rate;
    double total_miss_rate;
    int query_interior_size; // total size of non-boundary fragments
    int ref_interior_size; // total size of non-boundary fragments
    double interior_size_ratio;
    double query_scaling_factor;
    bool is_valid;
  };

  typedef std::vector<Alignment> AlignmentVec;

    // Sort an AlignmentVec in ascending order of score.
  class AlignmentRescaledScoreComp {
  public:
      bool operator()(const Alignment& a1, const Alignment& a2) {
        return a1.total_rescaled_score < a2.total_rescaled_score;
      }
  };

  // Sort an AlignmentVec in ascending order of score.
  class AlignmentScoreComp {
  public:
      bool operator()(const Alignment& a1, const Alignment& a2) {
        return a1.total_score < a2.total_score;
      }
  };

  extern const Alignment INVALID_ALIGNMENT;


  inline void Alignment::summarize() {

      query_misses = 0;
      ref_misses = 0;
      query_interior_size = 0;
      ref_interior_size = 0;
      num_matched_sites = 0;

      total_score = score.total();
      total_rescaled_score = rescaled_score.total();

      is_valid = !matched_chunks.empty();

      std::size_t l(matched_chunks.size());
      for (std::size_t i = 0; i < l; i++) {
        const MatchedChunk& mc = matched_chunks[i];
        query_misses += mc.query_chunk.num_misses();
        ref_misses += mc.ref_chunk.num_misses();
        if (!mc.query_chunk.is_boundary && !mc.ref_chunk.is_boundary) {
          query_interior_size += mc.query_chunk.size;
          ref_interior_size += mc.ref_chunk.size;
        }
      }

      query_scaling_factor = ((double) ref_interior_size) / query_interior_size;

      // Count the number of matched sites.
      //  - each non-boundary chunk begins/ends with a matched site.
      //  - The first chunk can potentially begin with a matched site, in local alignment.
      //  - The last chunk can potentially edn with a matched site, in local alignment.

      num_matched_sites = matched_chunks.size()-1;
      if (l > 0 && !matched_chunks[0].is_boundary()) {
        num_matched_sites++; // The first matched_chunk is not a boundary chunk
      }
      if (l > 1 && !matched_chunks[l-1].is_boundary()) {
        num_matched_sites++;
      }

      ref_miss_rate = ((double) ref_misses)/((double) num_matched_sites + ref_misses);
      query_miss_rate = ((double) query_misses)/((double) num_matched_sites + query_misses);
      total_miss_rate = ((double) ref_misses + query_misses) / ((double) ref_misses + query_misses + 2.0*num_matched_sites);
      interior_size_ratio = ((double) query_interior_size) / ref_interior_size;

  }

  inline void Alignment::reset_stats() {
      is_valid = false;
      query_misses = 0;
      ref_misses = 0;
      query_miss_rate = 0;
      ref_miss_rate = 0;
      total_miss_rate = 0;
      query_interior_size = 0;
      ref_interior_size = 0;
      num_matched_sites = 0;
      interior_size_ratio = 0;
      query_scaling_factor = 0;
      total_score = 0;
      total_rescaled_score = 0;
  }


}

#endif



 