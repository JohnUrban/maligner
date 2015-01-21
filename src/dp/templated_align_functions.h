#ifndef TEMPLATED_ALN_FUNC
#define TEMPLATED_ALN_FUNC

namespace maligner_dp {
/*
    Populate a score matrix using dynamic programming for ungapped alignment.

    The score matrix should have the same number of columns as the reference.

    The ScoreMatrix should already have the same nubmer of columns as the reference,
    and should have enough rows to accomodate the query.
  */
  template<class ScoreMatrixType>
  void fill_score_matrix(const AlignTask<ScoreMatrixType>& align_task) {

    // Unpack the alignment task
    const IntVec& query = *align_task.query;
    const IntVec& ref = *align_task.ref;
    ScoreMatrixType& mat = *align_task.mat;
    AlignOpts& align_opts = *align_task.align_opts;

    mat.reset();

    const int m = query.size() + 1;
    const int n = ref.size() + 1;

    // Note: Number of rows may be different from m if matrix is padded with extra rows.
    const int num_rows = mat.getNumRows();

    assert((int) mat.getNumCols() >= n);
    assert((int) mat.getNumRows() >= m);

    #if FILL_DEBUG > 0
    cerr << "m: " << m
         << " n: " << n
         << " num_rows: " << num_rows
         << " num_cols: " << mat.getNumCols()
         << "\n";
    #endif

    // Initialize the first row
    for (int j = 0; j < n; j++) {
      ScoreCell* pCell = mat.getCell(0,j);
      pCell->score_ = 0.0;
      pCell->backPointer_ = nullptr;
    }

    // Initialize the first column
    for (int i = 1; i < m; i++ ) {
      ScoreCell* pCell = mat.getCell(i,0);
      pCell->score_ = -INF;
      pCell->backPointer_ = nullptr;
    }

    // Initialize the body of the matrix.
    for (int j = 1; j < n; j++) {
      
      // Matrix is column major ordered.
      int offset = j*num_rows;

      for (int i = 1; i < m; i++) {
        ScoreCell* pCell = mat.getCell(offset + i);
        pCell->score_ = -INF;
        pCell->backPointer_ = nullptr;
      }

    }

    #if BREAKS_DEBUG > 0
    int num_breaks = 0;
    #endif


    for (int j = 1; j < n; j++) {
      
      int l0 = (j > align_opts.ref_max_misses + 1) ? j - align_opts.ref_max_misses - 1 : 0;
      
      const int offset = num_rows*j;
      
      for (int i = 1; i < m; i++) {

        ScoreCell* pCell = mat.getCell(offset + i);

        // Try all allowable extensions

        ScoreCell* backPointer = nullptr;
        double best_score = -INF;
        int k0 = (i > align_opts.query_max_misses) ? i - align_opts.query_max_misses - 1 : 0;
        int ref_size = 0;
        for(int l = j-1; l >= l0; l--) {

          const bool is_ref_boundary = l == 0 || j == n - 1;
          
          ref_size += ref[l];

          int ref_miss = j - l - 1; // sites in reference unaligned to query
          double ref_miss_score = ref_miss * align_opts.ref_miss_penalty;

          int query_size = 0;
          const int offset_back = num_rows*l;

          for(int k = i-1; k >= k0; k--) {

            const bool is_query_boundary =  k == 0 || k == m - 1;

            #if FILL_DEBUG > 0
            cerr << "i: " << i
                 << " j: " << j
                 << " k: " << k
                 << " l: " << l
                 << "\n";
            #endif


            query_size += query[k];
            ScoreCell* pTarget = mat.getCell(offset_back + k);
            if (pTarget->score_ == -INF) continue;

            int query_miss = i - k - 1; // sites in query unaligned to reference
            double query_miss_score = query_miss * align_opts.query_miss_penalty;

            // Add sizing penalty only if this is not a boundary fragment.
            double size_penalty = 0.0;
            if (!is_ref_boundary && !is_query_boundary) {
              size_penalty = sizing_penalty(query_size, ref_size, align_opts);
            }

            if (query_size > ref_size && size_penalty > align_opts.max_chunk_sizing_error) {
              // The query chunk is already too big for the reference chunk.
                #if BREAKS_DEBUG > 0
                  num_breaks++;
                #endif
              break;
            }

            // If the sizing penalty is too large, continue and do not populate matrix.
            if (size_penalty > align_opts.max_chunk_sizing_error) {
              continue;
            }

            double chunk_score = -size_penalty - query_miss_score - ref_miss_score;

            if (chunk_score + pTarget->score_ > best_score) {
              backPointer = pTarget;
              best_score = chunk_score + pTarget->score_;
            }

          } // for int k
        } // for int l

        // Assign the backpointer and score to pCell
        if (backPointer) {
          pCell->backPointer_ = backPointer;
          pCell->score_ = best_score;
        }

      } // for int i
    } // for int j
    
    #if BREAKS_DEBUG > 0
      std::cout << "num breaks: " << num_breaks << "\n";
    #endif

  } // fill_score_matrix

  template<class ScoreMatrixType>
  void fill_score_matrix_using_partials(const AlignTask<ScoreMatrixType>& align_task) {
    /*
    Fill score matrix using partial sums
    */

    // Unpack the alignment task
    AlignOpts& align_opts = *align_task.align_opts;
    const IntVec& query = *align_task.query;
    const IntVec& ref = *align_task.ref;
    const PartialSums& query_partial_sums = *align_task.query_partial_sums;
    const PartialSums& ref_partial_sums = *align_task.ref_partial_sums;
    const DoubleVec& ref_miss_penalties = align_opts.ref_miss_penalties;
    const DoubleVec& query_miss_penalties = align_opts.query_miss_penalties;

    ScoreMatrixType& mat = *align_task.mat;

    // mat.reset();


    const int m = query.size() + 1;
    const int n = ref.size() + 1;
    mat.resize(m, n);

    // Note: Number of rows may be different from m if matrix is padded with extra rows.
    const int num_rows = mat.getNumRows();

    assert((int) mat.getNumCols() >= n);
    assert((int) mat.getNumRows() >= m);

    #if FILL_DEBUG > 0
    cerr << "m: " << m
         << " n: " << n
         << " num_rows: " << num_rows
         << " num_cols: " << mat.getNumCols()
         << "\n";
    #endif

    // Initialize the first row
    for (int j = 0; j < n; j++) {
      ScoreCell* pCell = mat.getCell(0,j);
      pCell->score_ = 0.0;
      pCell->backPointer_ = nullptr;
    }

    // Initialize the first column
    for (int i = 1; i < m; i++ ) {
      ScoreCell* pCell = mat.getCell(i,0);
      pCell->score_ = -INF;
      pCell->backPointer_ = nullptr;
    }

    // Initialize the body of the matrix.
    for (int j = 1; j < n; j++) {
      
      // Matrix is column major ordered.
      int offset = j*num_rows;

      for (int i = 1; i < m; i++) {
        ScoreCell* pCell = mat.getCell(offset + i);
        pCell->score_ = -INF;
        pCell->backPointer_ = nullptr;
      }

    }

    #if BREAKS_DEBUG > 0
    int num_breaks = 0;
    #endif

    
    for (int j = 1; j < n; j++) {

      const IntVec& ref_ps = ref_partial_sums[j-1]; // reference partial sum

      int l0 = (j > align_opts.ref_max_misses + 1) ? j - align_opts.ref_max_misses - 1 : 0;
      
      for (int i = 1; i < m; i++) {

        const IntVec& query_ps = query_partial_sums[i-1]; // query partial sum

        ScoreCell* pCell = mat.getCell(i, j);

        // Try all allowable extensions

        ScoreCell* backPointer = nullptr;
        double best_score = -INF;
        int k0 = (i > align_opts.query_max_misses) ? i - align_opts.query_max_misses - 1 : 0;

        for(int l = j-1; l >= l0; l--) {

          const bool is_ref_boundary = !align_opts.ref_is_bounded && (l == 0 || j == n - 1);

          int ref_miss = j - l - 1; // sites in reference unaligned to query
          double ref_miss_score = ref_miss_penalties[ref_miss];
          int ref_size = ref_ps[ref_miss];

          for(int k = i-1; k >= k0; k--) {

            #if FILL_DEBUG > 0
            cerr << "i: " << i
                 << " j: " << j
                 << " k: " << k
                 << " l: " << l
                 << "\n";
            #endif

            ScoreCell* pTarget = mat.getCell(k, l);
            if (pTarget->score_ == -INF) continue;

            const bool is_query_boundary = !align_opts.query_is_bounded && (k == 0 || k == m - 1);

            int query_miss = i - k - 1; // sites in query unaligned to reference
            double query_miss_score = query_miss_penalties[query_miss];
            //double query_miss_score = query_miss * align_opts.query_miss_penalty;
            int query_size = query_ps[query_miss];

            // Add sizing penalty only if this is not a boundary fragment.
            double size_penalty = 0.0;
            if (!is_ref_boundary && !is_query_boundary) {

              size_penalty = sizing_penalty(query_size, ref_size, align_opts);
        
            }

            // Break if the query chunk is already too big for the reference
            if (size_penalty > align_opts.max_chunk_sizing_error) {

              // If query is too large, it will only get large and sizing error
              // will only increase, so we can break here.
              if(query_size > ref_size) {
                  #if BREAKS_DEBUG > 0
                    num_breaks++;
                  #endif
                break;
              }

              continue;

            }


            double chunk_score = -size_penalty - query_miss_score - ref_miss_score;

            if (chunk_score + pTarget->score_ > best_score) {
              backPointer = pTarget;
              best_score = chunk_score + pTarget->score_;
            }

          } // for int k
        } // for int l

        // Assign the backpointer and score to pCell
        if (backPointer) {
          pCell->backPointer_ = backPointer;
          pCell->score_ = best_score;
        }

      } // for int i
    } // for int j
    
    #if BREAKS_DEBUG > 0
      std::cout << "num breaks: " << num_breaks << "\n";
    #endif

  } // fill_score_matrix_using_partials


  template<class ScoreMatrixType>
  void fill_score_matrix_using_partials_with_cell_queue(const AlignTask<ScoreMatrixType>& align_task) {
    /*
    Fill score matrix using partial sums
    */

    // Unpack the alignment task
    const IntVec& query = *align_task.query;
    const IntVec& ref = *align_task.ref;
    const PartialSums& query_partial_sums = *align_task.query_partial_sums;
    const PartialSums& ref_partial_sums = *align_task.ref_partial_sums;

    typedef priority_queue< ScoreCell*, vector<ScoreCell*>, greater<ScoreCell*> > CellQueue;

    ScoreMatrixType& mat = *align_task.mat;
    mat.reset();
    AlignOpts& align_opts = *align_task.align_opts;

    // Compute the miss penalties
    IntVec query_miss_penalties(align_opts.query_max_misses+1, align_opts.query_miss_penalty);
    for (int i = 0; i < align_opts.query_max_misses+1; i++) {
      query_miss_penalties[i] *= (double) i;
    }

    IntVec ref_miss_penalties(align_opts.ref_max_misses+1, align_opts.ref_miss_penalty);
    for (int i = 0; i < align_opts.ref_max_misses+1; i++) {
      ref_miss_penalties[i] *= (double) i;
    }

    const int m = query.size() + 1;
    const int n = ref.size() + 1;

    // Note: Number of rows may be different from m if matrix is padded with extra rows.
    const int num_rows = mat.getNumRows();
    const int num_cols = mat.getNumCols();

    assert((int) mat.getNumCols() >= n);
    assert((int) mat.getNumRows() >= m);

    #if FILL_DEBUG > 0
    cerr << "m: " << m
         << " n: " << n
         << " num_rows: " << num_rows
         << " num_cols: " << mat.getNumCols()
         << "\n";
    #endif

    // Initialize the first column
    for (int i = 1; i < m; i++ ) {
      ScoreCell* pCell = mat.getCell(i,0);
      pCell->score_ = -INF;
      pCell->backPointer_ = nullptr;
      pCell->color_ = ScoreCellColor::BLACK;
    }

    // Initialize the body of the matrix.
    for (int j = 1; j < n; j++) {
      
      // Matrix is column major ordered.
      // int offset = j*num_rows;

      for (int i = 1; i < m; i++) {
        // ScoreCell* pCell = mat.getCell(offset + i);
        ScoreCell* pCell = mat.getCell(i, j);
        pCell->score_ = -INF;
        pCell->backPointer_ = nullptr;
        pCell->color_ = ScoreCellColor::WHITE;
      }

    }

    // Initialize the first row
    vector<ScoreCell*> for_queue;
    for (int j = 0; j < n; j++) {

      ScoreCell* pCell = mat.getCell(0,j);
      pCell->score_ = 0.0;
      pCell->backPointer_ = nullptr;

      for(int k = 1; k <= align_opts.query_max_misses + 1; k++) {
        for(int l = 1; l <= align_opts.ref_max_misses + 1; l++) {

          if(j + l >= num_cols) {continue;}
          if(k >= num_rows) {continue;}

          ScoreCell* cell_to_queue = mat.getCell(k, j+l);  
          #if QUEUE_DEBUG > 0
            std::cout << "checking cell: " << cell_to_queue->q_ << " " << cell_to_queue->r_
              << " " << cell_to_queue << " is white: " << (ScoreCellColor::WHITE == cell_to_queue->color_) << "\n";
          #endif
          if(cell_to_queue->color_ == ScoreCellColor::WHITE) {
            cell_to_queue->color_ = ScoreCellColor::BLACK;
            #if QUEUE_DEBUG > 0
              std::cout << "pushing cell: " << cell_to_queue->q_ << " " << cell_to_queue->r_
              << " " << cell_to_queue << "\n";
            #endif
            for_queue.push_back(cell_to_queue);
          }

        }
      }
    }

    CellQueue cell_queue(greater<ScoreCell*>(), std::move(for_queue));


    #if BREAKS_DEBUG > 0
    int num_breaks = 0;
    #endif

    #if QUEUE_DEBUG > 0
    std::cerr << "starting with cell queue: " << cell_queue.size() << "\n";
    #endif

    while(!cell_queue.empty()) {
      
      ScoreCell* pCell = cell_queue.top();
      cell_queue.pop();
      int i = pCell->q_;
      int j = pCell->r_;

      #if QUEUE_DEBUG > 0
      std::cout << "i: " << i << " j: " << j << " " << pCell << " cell_queue_size: " << cell_queue.size() << "\n";
      #endif

      const IntVec& ref_ps = ref_partial_sums[j-1]; // reference partial sum

      int l0 = (j > align_opts.ref_max_misses + 1) ? j - align_opts.ref_max_misses - 1 : 0;    
      const IntVec& query_ps = query_partial_sums[i-1]; // query partial sum

      // Try all allowable extensions

      ScoreCell* backPointer = nullptr;
      double best_score = -INF;
      int k0 = (i > align_opts.query_max_misses) ? i - align_opts.query_max_misses - 1 : 0;

      for(int l = j-1; l >= l0; l--) {

        const bool is_ref_boundary = (l == 0 || j == n - 1) && !align_opts.ref_is_bounded;

        int ref_miss = j - l - 1; // sites in reference unaligned to query
        double ref_miss_score = ref_miss_penalties[ref_miss];
        int ref_size = ref_ps[ref_miss];

        for(int k = i-1; k >= k0; k--) {

          const bool is_query_boundary =  (k == 0 || k == m - 1) && !align_opts.query_is_bounded;

          #if FILL_DEBUG > 0
          cerr << "i: " << i
               << " j: " << j
               << " k: " << k
               << " l: " << l
               << "\n";
          #endif

          ScoreCell* pTarget = mat.getCell(k, l);

          if (pTarget->score_ == -INF) continue;

          int query_miss = i - k - 1; // sites in query unaligned to reference
          double query_miss_score = query_miss_penalties[query_miss];
          //double query_miss_score = query_miss * align_opts.query_miss_penalty;
          int query_size = query_ps[query_miss];

          // Add sizing penalty only if this is not a boundary fragment.
          double size_penalty = 0.0;
          if (!is_ref_boundary && !is_query_boundary) {

            size_penalty = sizing_penalty(query_size, ref_size, align_opts);
      
          }

          // Break if the query chunk is already too big for the reference
          if (query_size > ref_size && size_penalty > align_opts.max_chunk_sizing_error) {
              #if BREAKS_DEBUG > 0
                num_breaks++;
              #endif
            break;
          }

          // If the sizing penalty is too large, continue and do not populate matrix.
          if (size_penalty > align_opts.max_chunk_sizing_error) {
            continue;
          }

          double chunk_score = -size_penalty - query_miss_score - ref_miss_score;

          if (chunk_score + pTarget->score_ > best_score) {
            backPointer = pTarget;
            best_score = chunk_score + pTarget->score_;
          }

        } // for int k
      } // for int l

      // Assign the backpointer and score to pCell
      if (backPointer) {

        pCell->backPointer_ = backPointer;
        pCell->score_ = best_score;

        // Queue the successors if this cell, which are in play.
        int best_row = pCell->q_;
        int best_col = pCell->r_;
        for(int k = 1; k <= align_opts.query_max_misses + 1; k++) {
          if(best_row + k >= num_rows) break;
          for(int l = 1; l <= align_opts.ref_max_misses + 1; l++) {
            if(best_col + l >= num_cols) break;
            ScoreCell* cell_to_queue = mat.getCell(best_row + k, best_col + l);
            #if QUEUE_DEBUG > 0
              std::cout << "checking cell: " << cell_to_queue->q_ << " " << cell_to_queue->r_
                << " " << cell_to_queue << " is white: " << (ScoreCellColor::WHITE == cell_to_queue->color_) << "\n";
            #endif
            if(cell_to_queue->color_ == ScoreCellColor::WHITE) {
              cell_to_queue->color_ = ScoreCellColor::BLACK;
              #if QUEUE_DEBUG > 0
              std::cout << "pushing cell: " << cell_to_queue->q_ << " " << cell_to_queue->r_
                << " " << cell_to_queue << "\n";
              #endif
              cell_queue.push(cell_to_queue);
            }
          }
        }

      }
    }

    #if BREAKS_DEBUG > 0
      std::cout << "num breaks: " << num_breaks << "\n";
    #endif

  } // fill_score_matrix_using_partials

  template<class ScoreMatrixType>
  void fill_score_matrix_using_partials_with_cell_mark(const AlignTask<ScoreMatrixType>& align_task) {
    /*
    Fill score matrix using partial sums
    */

    // Unpack the alignment task
    const IntVec& query = *align_task.query;
    const IntVec& ref = *align_task.ref;
    const PartialSums& query_partial_sums = *align_task.query_partial_sums;
    const PartialSums& ref_partial_sums = *align_task.ref_partial_sums;


    ScoreMatrixType& mat = *align_task.mat;
    mat.reset();
    AlignOpts& align_opts = *align_task.align_opts;

    // Compute the miss penalties
    IntVec query_miss_penalties(align_opts.query_max_misses+1, align_opts.query_miss_penalty);
    for (int i = 0; i < align_opts.query_max_misses+1; i++) {
      query_miss_penalties[i] *= (double) i;
    }

    IntVec ref_miss_penalties(align_opts.ref_max_misses+1, align_opts.ref_miss_penalty);
    for (int i = 0; i < align_opts.ref_max_misses+1; i++) {
      ref_miss_penalties[i] *= (double) i;
    }

    const int m = query.size() + 1;
    const int n = ref.size() + 1;

    // Note: Number of rows may be different from m if matrix is padded with extra rows.
    const int num_rows = mat.getNumRows();
    const int num_cols = mat.getNumCols();

    assert((int) mat.getNumCols() >= n);
    assert((int) mat.getNumRows() >= m);

    #if FILL_DEBUG > 0
    cerr << "m: " << m
         << " n: " << n
         << " num_rows: " << num_rows
         << " num_cols: " << mat.getNumCols()
         << "\n";
    #endif

    // Initialize the first column
    for (int i = 1; i < m; i++ ) {
      ScoreCell* pCell = mat.getCell(i,0);
      pCell->score_ = -INF;
      pCell->backPointer_ = nullptr;
      pCell->color_ = ScoreCellColor::BLACK;
    }

    // Initialize the body of the matrix.
    for (int j = 1; j < n; j++) {
      
      // Matrix is column major ordered.
      // int offset = j*num_rows;

      for (int i = 1; i < m; i++) {
        // ScoreCell* pCell = mat.getCell(offset + i);
        ScoreCell* pCell = mat.getCell(i, j);
        pCell->score_ = -INF;
        pCell->backPointer_ = nullptr;
        pCell->color_ = ScoreCellColor::WHITE;
      }

    }

    const ScoreCellColor COLOR_IN_PLAY = ScoreCellColor::GREEN;

    // Initialize the first row
    for (int j = 0; j < n; j++) {

      ScoreCell* pCell = mat.getCell(0,j);
      pCell->score_ = 0.0;
      pCell->backPointer_ = nullptr;

      for(int k = 1; k <= align_opts.query_max_misses + 1; k++) {
        for(int l = 1; l <= align_opts.ref_max_misses + 1; l++) {

          if(j + l >= num_cols) {continue;}
          if(k >= num_rows) {continue;}

          ScoreCell* cell_to_queue = mat.getCell(k, j+l);  
          cell_to_queue->color_ = COLOR_IN_PLAY;

        }
      }
    }



    #if BREAKS_DEBUG > 0
    int num_breaks = 0;
    #endif

    for (int j = 1; j < n; j++) {

      const IntVec& ref_ps = ref_partial_sums[j-1]; // reference partial sum

      int l0 = (j > align_opts.ref_max_misses + 1) ? j - align_opts.ref_max_misses - 1 : 0;
      const int offset = num_rows*j;
      
      for (int i = 1; i < m; i++) {

        ScoreCell* pCell = mat.getCell(i, j);

        if (COLOR_IN_PLAY != pCell->color_) { continue; }

        const IntVec& query_ps = query_partial_sums[i-1]; // query partial sum

        // Try all allowable extensions

        ScoreCell* backPointer = nullptr;
        double best_score = -INF;
        int k0 = (i > align_opts.query_max_misses) ? i - align_opts.query_max_misses - 1 : 0;

        for(int l = j-1; l >= l0; l--) {

          const bool is_ref_boundary = (l == 0 || j == n - 1) && !align_opts.ref_is_bounded;

          int ref_miss = j - l - 1; // sites in reference unaligned to query
          double ref_miss_score = ref_miss_penalties[ref_miss];
          int ref_size = ref_ps[ref_miss];

          for(int k = i-1; k >= k0; k--) {

            const bool is_query_boundary =  (k == 0 || k == m - 1) && !align_opts.query_is_bounded;

            #if FILL_DEBUG > 0
            cerr << "i: " << i
                 << " j: " << j
                 << " k: " << k
                 << " l: " << l
                 << "\n";
            #endif

            ScoreCell* pTarget = mat.getCell(k, l);

            if (pTarget->score_ == -INF) continue;

            int query_miss = i - k - 1; // sites in query unaligned to reference
            double query_miss_score = query_miss_penalties[query_miss];
            //double query_miss_score = query_miss * align_opts.query_miss_penalty;
            int query_size = query_ps[query_miss];

            // Add sizing penalty only if this is not a boundary fragment.
            double size_penalty = 0.0;
            if (!is_ref_boundary && !is_query_boundary) {

              size_penalty = sizing_penalty(query_size, ref_size, align_opts);
        
            }

            // Break if the query chunk is already too big for the reference
            if (size_penalty > align_opts.max_chunk_sizing_error) {

              // If query is already too large, we can break.
              if(query_size > ref_size) {
                  #if BREAKS_DEBUG > 0
                    num_breaks++;
                  #endif
                break;
              }

              // If the sizing penalty is too large, continue and do not populate matrix.
              if (size_penalty > align_opts.max_chunk_sizing_error) {
                continue;
              }

            }

            double chunk_score = -size_penalty - query_miss_score - ref_miss_score;

            if (chunk_score + pTarget->score_ > best_score) {
              backPointer = pTarget;
              best_score = chunk_score + pTarget->score_;
            }

          } // for int k
        } // for int l

        // Assign the backpointer and score to pCell
        if (backPointer) {

          pCell->backPointer_ = backPointer;
          pCell->score_ = best_score;

          // Queue the successors if this cell, which are in play.
          int best_row = pCell->q_;
          int best_col = pCell->r_;
          for(int k = 1; k <= align_opts.query_max_misses + 1; k++) {
            if(best_row + k >= num_rows) break;
            for(int l = 1; l <= align_opts.ref_max_misses + 1; l++) {
              if(best_col + l >= num_cols) break;
              ScoreCell* cell_to_queue = mat.getCell(best_row + k, best_col + l);
              #if QUEUE_DEBUG > 0
                std::cout << "checking cell: " << cell_to_queue->q_ << " " << cell_to_queue->r_
                  << " " << cell_to_queue << " is white: " << (ScoreCellColor::WHITE == cell_to_queue->color_) << "\n";
              #endif

              cell_to_queue->color_ = COLOR_IN_PLAY;

            }
          }
        }

      }
    }

    #if BREAKS_DEBUG > 0
      std::cout << "num breaks: " << num_breaks << "\n";
    #endif

  } // fill_score_matrix_using_partials









  template<class ScoreMatrixType>
  void fill_score_matrix_using_partials_no_size_penalty(const AlignTask<ScoreMatrixType>& align_task) {
    /*
    Fill score matrix using partial sums
    */

    // Unpack the alignment task
    const IntVec& query = *align_task.query;
    const IntVec& ref = *align_task.ref;
    const PartialSums& query_partial_sums = *align_task.query_partial_sums;
    const PartialSums& ref_partial_sums = *align_task.ref_partial_sums;

    ScoreMatrixType& mat = *align_task.mat;
    mat.reset();
    AlignOpts& align_opts = *align_task.align_opts;

    // Compute the miss penalties
    IntVec query_miss_penalties(align_opts.query_max_misses+1, align_opts.query_miss_penalty);
    for (int i = 0; i < align_opts.query_max_misses+1; i++) {
      query_miss_penalties[i] *= (double) i;
    }

    IntVec ref_miss_penalties(align_opts.ref_max_misses+1, align_opts.ref_miss_penalty);
    for (int i = 0; i < align_opts.ref_max_misses+1; i++) {
      ref_miss_penalties[i] *= (double) i;
    }

    const int m = query.size() + 1;
    const int n = ref.size() + 1;

    // Note: Number of rows may be different from m if matrix is padded with extra rows.
    const int num_rows = mat.getNumRows();

    assert((int) mat.getNumCols() >= n);
    assert((int) mat.getNumRows() >= m);

    #if FILL_DEBUG > 0
    cerr << "m: " << m
         << " n: " << n
         << " num_rows: " << num_rows
         << " num_cols: " << mat.getNumCols()
         << "\n";
    #endif

    // Initialize the first row
    for (int j = 0; j < n; j++) {
      ScoreCell* pCell = mat.getCell(0,j);
      pCell->score_ = 0.0;
      pCell->backPointer_ = nullptr;
    }

    // Initialize the first column
    for (int i = 1; i < m; i++ ) {
      ScoreCell* pCell = mat.getCell(i,0);
      pCell->score_ = -INF;
      pCell->backPointer_ = nullptr;
    }

    // Initialize the body of the matrix.
    for (int j = 1; j < n; j++) {
      
      // Matrix is column major ordered.
      int offset = j*num_rows;

      for (int i = 1; i < m; i++) {
        ScoreCell* pCell = mat.getCell(offset + i);
        pCell->score_ = -INF;
        pCell->backPointer_ = nullptr;
      }

    }

    #if BREAKS_DEBUG > 0
    int num_breaks = 0;
    #endif

    
    for (int j = 1; j < n; j++) {

      const IntVec& ref_ps = ref_partial_sums[j-1]; // reference partial sum

      int l0 = (j > align_opts.ref_max_misses + 1) ? j - align_opts.ref_max_misses - 1 : 0;
      const int offset = num_rows*j;
      
      for (int i = 1; i < m; i++) {

        const IntVec& query_ps = query_partial_sums[i-1]; // query partial sum

        ScoreCell* pCell = mat.getCell(offset + i);

        // Try all allowable extensions

        ScoreCell* backPointer = nullptr;
        double best_score = -INF;
        int k0 = (i > align_opts.query_max_misses) ? i - align_opts.query_max_misses - 1 : 0;

        for(int l = j-1; l >= l0; l--) {

          const bool is_ref_boundary = (l == 0 || j == n - 1) && !align_opts.ref_is_bounded;

          int ref_miss = j - l - 1; // sites in reference unaligned to query
          double ref_miss_score = ref_miss_penalties[ref_miss];
          int ref_size = ref_ps[ref_miss];

          const int offset_back = num_rows*l;

          for(int k = i-1; k >= k0; k--) {

            const bool is_query_boundary =  (k == 0 || k == m - 1) && !align_opts.query_is_bounded;

            #if FILL_DEBUG > 0
            cerr << "i: " << i
                 << " j: " << j
                 << " k: " << k
                 << " l: " << l
                 << "\n";
            #endif

            ScoreCell* pTarget = mat.getCell(offset_back + k);
            if (pTarget->score_ == -INF) continue;

            int query_miss = i - k - 1; // sites in query unaligned to reference
            double query_miss_score = query_miss_penalties[query_miss];
            //double query_miss_score = query_miss * align_opts.query_miss_penalty;
            int query_size = query_ps[query_miss];

            // Add sizing penalty only if this is not a boundary fragment.
            double size_penalty = 0.0;
            // if (!is_ref_boundary && !is_query_boundary) {

            //   size_penalty = sizing_penalty(query_size, ref_size, align_opts);
        
            // }

            // Break if the query chunk is already too big for the reference
            if (query_size > ref_size && size_penalty > align_opts.max_chunk_sizing_error) {
                #if BREAKS_DEBUG > 0
                  num_breaks++;
                #endif
              break;
            }

            // If the sizing penalty is too large, continue and do not populate matrix.
            if (size_penalty > align_opts.max_chunk_sizing_error) {
              continue;
            }

            double chunk_score = -size_penalty - query_miss_score - ref_miss_score;

            if (chunk_score + pTarget->score_ > best_score) {
              backPointer = pTarget;
              best_score = chunk_score + pTarget->score_;
            }

          } // for int k
        } // for int l

        // Assign the backpointer and score to pCell
        if (backPointer) {
          pCell->backPointer_ = backPointer;
          pCell->score_ = best_score;
        }

      } // for int i
    } // for int j
    
    #if BREAKS_DEBUG > 0
      std::cout << "num breaks: " << num_breaks << "\n";
    #endif

  } // fill_score_matrix_using_partials_no_size_penalty

  template<class ScoreMatrixType>
  void fill_score_matrix_using_partials_with_size_penalty_class(const AlignTask<ScoreMatrixType>& align_task) {
    /*
    Fill score matrix using partial sums
    */

    // Unpack the alignment task
    const IntVec& query = *align_task.query;
    const IntVec& ref = *align_task.ref;
    const PartialSums& query_partial_sums = *align_task.query_partial_sums;
    const PartialSums& ref_partial_sums = *align_task.ref_partial_sums;

    ScoreMatrixType& mat = *align_task.mat;
    mat.reset();
    AlignOpts& align_opts = *align_task.align_opts;

    // Create a vector for computing sizing error
    vector<SizingPenalty> sizing_penalties;
    sizing_penalties.reserve(ref.size()*(align_opts.ref_max_misses+1));
    for(size_t i = 0; i < ref.size(); i++) {
      int cur_size = 0;
      for(int m = 0; m <= align_opts.ref_max_misses; m++) {
        if (i >= size_t(m)) cur_size += ref[i-m];
        sizing_penalties.emplace_back(cur_size, align_opts);
      }
    }

    // Compute the miss penalties
    IntVec query_miss_penalties(align_opts.query_max_misses+1, align_opts.query_miss_penalty);
    for (int i = 0; i < align_opts.query_max_misses+1; i++) {
      query_miss_penalties[i] *= (double) i;
    }

    IntVec ref_miss_penalties(align_opts.ref_max_misses+1, align_opts.ref_miss_penalty);
    for (int i = 0; i < align_opts.ref_max_misses+1; i++) {
      ref_miss_penalties[i] *= (double) i;
    }

    const int m = query.size() + 1;
    const int n = ref.size() + 1;

    // Note: Number of rows may be different from m if matrix is padded with extra rows.
    const int num_rows = mat.getNumRows();

    assert((int) mat.getNumCols() >= n);
    assert((int) mat.getNumRows() >= m);

    #if FILL_DEBUG > 0
    cerr << "m: " << m
         << " n: " << n
         << " num_rows: " << num_rows
         << " num_cols: " << mat.getNumCols()
         << "\n";
    #endif

    // Initialize the first row
    for (int j = 0; j < n; j++) {
      ScoreCell* pCell = mat.getCell(0,j);
      pCell->score_ = 0.0;
      pCell->backPointer_ = nullptr;
    }

    // Initialize the first column
    for (int i = 1; i < m; i++ ) {
      ScoreCell* pCell = mat.getCell(i,0);
      pCell->score_ = -INF;
      pCell->backPointer_ = nullptr;
    }

    // Initialize the body of the matrix.
    for (int j = 1; j < n; j++) {
      
      // Matrix is column major ordered.
      int offset = j*num_rows;

      for (int i = 1; i < m; i++) {
        ScoreCell* pCell = mat.getCell(offset + i);
        pCell->score_ = -INF;
        pCell->backPointer_ = nullptr;
      }

    }

    #if BREAKS_DEBUG > 0
    int num_breaks = 0;
    #endif

    
    for (int j = 1; j < n; j++) {

      const IntVec& ref_ps = ref_partial_sums[j-1]; // reference partial sum

      int l0 = (j > align_opts.ref_max_misses + 1) ? j - align_opts.ref_max_misses - 1 : 0;
      const int offset = num_rows*j;
      
      for (int i = 1; i < m; i++) {

        const IntVec& query_ps = query_partial_sums[i-1]; // query partial sum

        ScoreCell* pCell = mat.getCell(offset + i);

        // Try all allowable extensions

        ScoreCell* backPointer = nullptr;
        double best_score = -INF;
        int k0 = (i > align_opts.query_max_misses) ? i - align_opts.query_max_misses - 1 : 0;

        for(int l = j-1; l >= l0; l--) {

          const bool is_ref_boundary = (l == 0 || j == n - 1) && !align_opts.ref_is_bounded;

          int ref_miss = j - l - 1; // sites in reference unaligned to query
          double ref_miss_score = ref_miss_penalties[ref_miss];
          int ref_size = ref_ps[ref_miss];

          const int offset_back = num_rows*l;

          for(int k = i-1; k >= k0; k--) {

            const bool is_query_boundary =  (k == 0 || k == m - 1) && !align_opts.query_is_bounded;

            #if FILL_DEBUG > 0
            cerr << "i: " << i
                 << " j: " << j
                 << " k: " << k
                 << " l: " << l
                 << "\n";
            #endif

            ScoreCell* pTarget = mat.getCell(offset_back + k);
            if (pTarget->score_ == -INF) continue;

            int query_miss = i - k - 1; // sites in query unaligned to reference
            double query_miss_score = query_miss_penalties[query_miss];
            //double query_miss_score = query_miss * align_opts.query_miss_penalty;
            int query_size = query_ps[query_miss];

            // Add sizing penalty only if this is not a boundary fragment.
            double size_penalty = 0.0;
            if (!is_ref_boundary && !is_query_boundary) {
              SizingPenalty& sizing_penalty_op = sizing_penalties[(j-1)*(align_opts.ref_max_misses+1) + ref_miss];
              size_penalty = sizing_penalty_op(query_size);
              // double size_penalty2 = sizing_penalty(query_size, ref_size, align_opts);
              // std::cerr << "ref_size1: " << sizing_penalty_op.ref_size_ << " ref_size2: " << ref_size << " ";
              // std::cerr << "size_penalty1: " << size_penalty << "\tsize_penalty2: " << size_penalty2 << "\n";
              // assert(size_penalty == size_penalty2);
            }

            // Break if the query chunk is already too big for the reference
            if (query_size > ref_size && size_penalty > align_opts.max_chunk_sizing_error) {
                #if BREAKS_DEBUG > 0
                  num_breaks++;
                #endif
              break;
            }

            // If the sizing penalty is too large, continue and do not populate matrix.
            if (size_penalty > align_opts.max_chunk_sizing_error) {
              continue;
            }

            double chunk_score = -size_penalty - query_miss_score - ref_miss_score;

            if (chunk_score + pTarget->score_ > best_score) {
              backPointer = pTarget;
              best_score = chunk_score + pTarget->score_;
            }

          } // for int k
        } // for int l

        // Assign the backpointer and score to pCell
        if (backPointer) {
          pCell->backPointer_ = backPointer;
          pCell->score_ = best_score;
        }

      } // for int i
    } // for int j
    
    #if BREAKS_DEBUG > 0
      std::cout << "num breaks: " << num_breaks << "\n";
    #endif

  } // fill_score_matrix_using_partials_with_size_penalty_class


  template<class ScoreMatrixType>
  bool get_best_alignment_trail(const AlignTask<ScoreMatrixType>& task, ScoreCellPVec& trail) {

    // Go to the last row of the ScoreMatrix and identify the best score.
    const IntVec& query = *task.query;
    const IntVec& ref = *task.ref;
    ScoreMatrixType& mat = *task.mat;

    const int m = query.size() + 1;
    const int n = ref.size() + 1;
    const int num_rows = mat.getNumRows();
    const int num_cols = mat.getNumCols();
    const int last_row = m - 1;

    double best_score = -INF;
    ScoreCell * p_best_cell = nullptr;
    int index = last_row;

    // Get the cell with the best score in the last row.
    for (int i = 0; i < n; i++, index += num_rows) {
      
      ScoreCell * pCell = mat.getCell(index);

      #if GET_BEST_DEBUG > 0
      cerr << "index: " << index << ", ";
      cerr << "cell: " << *pCell << "\n";
      #endif

      if (pCell->score_ > best_score) {
        p_best_cell = pCell;
        best_score = pCell->score_;
      }
    }

    #if GET_BEST_DEBUG > 0
      if (p_best_cell) {
          cerr << "\np_best_cell: " << *p_best_cell <<  "\n";
      } else {
          cerr << "\np_best_cell: " << p_best_cell <<  "\n";
      }
    #endif
   
    if (p_best_cell && p_best_cell->score_ > -INF) {
      // Get the traceback
      trail.clear();
      trail.reserve(m);
      build_trail(p_best_cell, trail);
      return true;
    }

    return false;

  }

  /////////////////////////////////////////////////////////////////////
  // Get the best n alignments in the task.
  // The score matrix should already be filled out.
  //
  // Append trail seeds to the trail_seeds in the alignment task.
  // Return True if alignments were found.
  template<class ScoreMatrixType>
  int get_best_alignments(const AlignTask<ScoreMatrixType>& task) {

    // Go to the last row of the ScoreMatrix and identify the best score.
    AlignOpts& align_opts = *task.align_opts;
    const IntVec& query = *task.query;
    const IntVec& ref = *task.ref;
    ScoreMatrixType& mat = *task.mat;
    AlignmentVec& alignments = *task.alignments;
    AlignmentVec neighborhood_alignments;
    neighborhood_alignments.reserve(1 + 2 * align_opts.neighbor_delta );
    AlignmentRescaledScoreComp alignment_rescaled_score_comp;

    const int m = query.size() + 1;
    const int n = ref.size() + 1;
    const int num_rows = mat.getNumRows();
    const int num_cols = mat.getNumCols();
    const int last_row = m - 1;

    BitCover cells_covered(n);

    size_t max_alignments = align_opts.alignments_per_reference * ( 1 + 2 * align_opts.neighbor_delta );
    alignments.reserve(max_alignments);

    ScoreCellPVec alignment_seeds;
    alignment_seeds.reserve(n);

    // Go to the last row in the matrix, and get the best score cells.
    for (int i = 0; i < n; i++) {
      ScoreCell * pCell = mat.getCell(last_row, i);
      bool have_alignment = pCell && pCell->score_ > -INF && pCell->backPointer_;
      if (have_alignment) {
        alignment_seeds.push_back(pCell);
      }
    }

    // Sort the alignment seeds in descending order of score
    std::sort(alignment_seeds.begin(), alignment_seeds.end(), ScoreCellPointerCmp);

    /////////////////////////////////////////////////////////////////////////////////
    // For each alignment seed, build the Alignment.
    // Rescale the alignment if necessary.
    // Select non-overlapping alignments.
    const size_t num_seeds = alignment_seeds.size();
    int num_seeds_selected = 0;
    for (int s = 0;
         s < num_seeds && num_seeds_selected < align_opts.max_alignment_seeds;
         s++)
    {

      ScoreCell * pCell = alignment_seeds[s];
      int seed_col = pCell->r_;
      if (cells_covered[seed_col]) continue;

      // Build the alignment
      Alignment a(alignment_from_cell(task, pCell));

      // If the alignment overlaps an existing alignment, ignore.
      // TODO: We select alignments to build based on the *non*-rescaled score.
      // Should we build all alignments, rescale, sort based on rescaled score,
      // and select non-overlapping alignments based on rescaled score?
      // This will be a little extra computation.
      if ( cells_covered.is_covered(a.ref_start(), a.ref_end()) ) {
        continue;
      }

      num_seeds_selected++;

      // Retrieve alignments from neighbors, if requested, and select the 
      // one with the best rescaled score.
      if(align_opts.neighbor_delta > 0)
      {

        // Do a neighborhood search for other alignments in the vicinity of this one.
        neighborhood_alignments.clear();
        neighborhood_alignments.push_back(std::move(a));

        int lb = max(seed_col - align_opts.neighbor_delta, 0);
        int ub = min(seed_col + align_opts.neighbor_delta + 1, n);
        for (int col = lb; col < ub; col++) {
          if (col == seed_col) continue;
          ScoreCell * p_neighbor = mat.getCell(last_row, col);
          bool have_alignment = p_neighbor->score_ > -INF && p_neighbor->backPointer_;
          if (!have_alignment) continue;
          Alignment a(alignment_from_cell(task, p_neighbor));
          neighborhood_alignments.push_back(std::move(a));
        }

        // Sort the neighborhood alignments in ascending order of rescaled score.
        std::sort(neighborhood_alignments.begin(),
                  neighborhood_alignments.end(),
                  alignment_rescaled_score_comp);
        
        #if NEIGHBORHOOD_DEBUG > 0
        {
        Alignment& a0 = neighborhood_alignments[0];
        Alignment& al = neighborhood_alignments.back();
        std::cerr << "Neighborhood: seed_col: " << seed_col << " lb: " << lb << " ub: " << ub << "\n";

        std::cerr << "Num alignments in neighborhood: " << neighborhood_alignments.size() << "\n";
        std::cerr << "First: " << a0.rescaled_matched_chunks.back().ref_chunk.end << " score: " << a0.score << "rescaled: " << a0.rescaled_score << " total_rescaled: " << a0.total_rescaled_score << "\n";
        std::cerr << "Last: " << a1.rescaled_matched_chunks.back().ref_chunk.end << " score: " << a1.score << "rescaled: " << a1.rescaled_score << " total_rescaled: " << a1.total_rescaled_score << "\n";
        }
        #endif


        // Save the best alignment (i.e. with the lowest "edit distance" score)
        assert(neighborhood_alignments.size() > 0);
        Alignment& a = neighborhood_alignments[0];
        cells_covered.cover(a.ref_start(), a.ref_end());
        alignments.push_back(std::move(a));

      } else { // if neighbor_delta > 0
        cells_covered.cover(a.ref_start(), a.ref_end());
        alignments.push_back(std::move(a));
      }

      // Mark neighboring cells as out of play.
      {
        int lb = max(seed_col - align_opts.min_alignment_spacing + 1, 0);
        int ub = min(seed_col + align_opts.min_alignment_spacing, n);
        cells_covered.cover(lb, ub);
      }

    }

    return num_seeds_selected;
  }


  /////////////////////////////////////////////////////////////////////
  // Get the best alignments in the task.
  // Try *all* alignment seeds (disregard any limits on the number of alignment seeds or
  //   alignments per reference)
  // Rescale *all* alignments
  // Select non-overlapping alignments in order of rescaled score.
  template<class ScoreMatrixType>
  int get_best_alignments_try_all(const AlignTask<ScoreMatrixType>& task) {

    // Go to the last row of the ScoreMatrix and identify the best score.
    AlignOpts& align_opts = *task.align_opts;
    const IntVec& query = *task.query;
    const IntVec& ref = *task.ref;
    ScoreMatrixType& mat = *task.mat;
    AlignmentVec& alignments = *task.alignments;
    
    AlignmentRescaledScoreComp alignment_rescaled_score_comp;

    const int m = query.size() + 1;
    const int n = ref.size() + 1;
    const int num_rows = mat.getNumRows();
    const int num_cols = mat.getNumCols();
    const int last_row = m - 1;

    // Go to the last row in the matrix, and get all alignments.
    AlignmentVec my_alignments;
    for (int i = 0; i < n; i++) {
      ScoreCell * pCell = mat.getCell(last_row, i);
      bool have_alignment = pCell && pCell->score_ > -INF && pCell->backPointer_;
      if (have_alignment) {
        Alignment a = alignment_from_cell(task, pCell);
        my_alignments.push_back(std::move(a));
      }
    }

    // Sort the alignment in descending order score
    std::sort(my_alignments.begin(), my_alignments.end(), AlignmentRescaledScoreComp());
    
    int num_alignments = 0;
    BitCover cells_covered(n);
    const size_t N = my_alignments.size();
    for(size_t i = 0; i < N; i++) {

      Alignment& aln = my_alignments[i];
      if(cells_covered.is_covered(aln.ref_start(), aln.ref_end())) {
        continue;
      }

      cells_covered.cover(aln.ref_start(), aln.ref_end());
      alignments.push_back(std::move(aln));
      num_alignments++;

    }

    return num_alignments;
  }

    // trail: starts from end of alignment
  template<class ScoreMatrixType>
  void build_chunk_trail(const AlignTask<ScoreMatrixType>& task, ScoreCellPVec& trail, ChunkVec& query_chunks, ChunkVec& ref_chunks) {

    const int ts = trail.size();
    if (ts == 0) { return; }

    const IntVec& query = *task.query;
    const IntVec& ref = *task.ref;
    const AlignOpts& align_opts = *task.align_opts;
    const PartialSums& query_partial_sums = *task.query_partial_sums;
    const PartialSums& ref_partial_sums = *task.ref_partial_sums;

    query_chunks.clear();
    query_chunks.reserve(ts-1);
    ref_chunks.clear();
    ref_chunks.reserve(ts-1);
    //assert(query_chunks.size() == ts - 1);
    //assert(ref_chunks.size() == ts - 1);

    ScoreCell* pLast = trail[0];
    int ml = pLast->q_;

    #if DEBUG > 0
    std::cerr << "build_chunk_trail for: " << trail << std::endl;
    #endif

    int nl = pLast->r_;

    auto qi(query_chunks.begin());
    auto ri(ref_chunks.begin());
    for(int i = 1; i < ts; i++, qi++, ri++) {
      
      const ScoreCell* pCell = trail[i];
      int m = pCell->q_; // index of query site, one based
      int n = pCell->r_; // index of ref site, one based

      #if DEBUG > 0
      cerr << "cell: " << pCell << " " << *pCell << "\n"
           << " i: " << i 
           << " m: " << m << " ml: " << ml
           << " n: " << n << " nl: " << nl
           << "\n";
      #endif

      assert(m < ml);
      assert(n < nl);

      bool is_query_boundary = (m == 0 || ml == query.size()) && !align_opts.query_is_bounded;
      bool is_ref_boundary = !align_opts.ref_is_bounded && ( (n + task.ref_offset == 0) || 
          (nl + task.ref_offset == task.ref_map_data->num_frags_) );

      // Build chunks. Specify indices of chunks with respect to the original maps,
      // if aligning a slice of the map.

      // Commenting out, use partial sums instead
      // int q_size = sum(query, m, ml);
      // int r_size = sum(ref, n, nl);

      int q_size = query_partial_sums[ml - 1][ml - m - 1];
      int r_size = ref_partial_sums[nl - 1][nl - n - 1];

      query_chunks.emplace_back(m, ml, q_size, is_query_boundary);
      ref_chunks.emplace_back(n + task.ref_offset, nl + task.ref_offset, r_size, is_ref_boundary);

      ml = m;
      nl = n;
    }
  }

  // Make and return an alignment from the trail through the
  // score matrix. 
  template<class ScoreMatrixType>
  Alignment alignment_from_trail(const AlignTask<ScoreMatrixType>& task, ScoreCellPVec& trail) {

      const AlignOpts& align_opts = *task.align_opts;
      const IntVec& query = *task.query;

      const size_t chunk_vec_size = trail.size() - 1;

      ChunkVec query_chunks, ref_chunks;
      MatchedChunkVec matched_chunks;

      build_chunk_trail(task, trail, query_chunks, ref_chunks);

      // The trail and the chunks are oriented from the end of the alignment.
      // Build MatchChunk's in the forward direction.
      assert(query_chunks.size() == ref_chunks.size());

      const size_t n = query_chunks.size();
      const int num_query_frags = query.size();

      Score total_score(0.0, 0.0, 0.0);
      matched_chunks.reserve(n);
      for (int i = n-1; i >= 0; i--) {
        
          Chunk& qc = query_chunks[i];
          Chunk& rc = ref_chunks[i];
          int ref_misses = rc.end - rc.start - 1; // sites in reference that are unaligned to query
          int query_misses = qc.end - qc.start - 1; // sites in query that are unaligned to reference

          double query_miss_score = align_opts.query_miss_penalty * query_misses;
          double ref_miss_score = align_opts.ref_miss_penalty * ref_misses;
          double sizing_score = 0.0;

          const bool query_is_boundary = (qc.start == 0 || qc.end == num_query_frags) && !align_opts.query_is_bounded;

          if (!query_is_boundary) {
            sizing_score = sizing_penalty(qc.size, rc.size, align_opts);
          }

          Score score(query_miss_score, ref_miss_score, sizing_score);

          total_score.query_miss_score += query_miss_score;
          total_score.ref_miss_score += ref_miss_score;
          total_score.sizing_score += sizing_score;

          // MatchedChunk mc(query_chunks[i], ref_chunks[i], score);
          // Try fancy C++11:
          matched_chunks.emplace_back(query_chunks[i], ref_chunks[i], score);
          
      }

      // return Alignment(std::move(matched_chunks), total_score);
      return Alignment(matched_chunks, total_score,
             *task.query_map_data, *task.ref_map_data,
             task.is_forward);
  }

  template<class ScoreMatrixType>
  Alignment alignment_from_cell(const AlignTask<ScoreMatrixType>& task, ScoreCell* p_cell) {
    
    const size_t m = task.query->size() + 1;
    const AlignOpts& align_opts = *task.align_opts;

    ScoreCellPVec trail;
    trail.reserve(m);
    build_trail(p_cell, trail);

    Alignment aln = alignment_from_trail(task, trail);

    if (align_opts.rescale_query) {
      aln.rescale_matched_chunks(align_opts);
    }

    return aln;
  }

    //////////////////////////////////////////////////////////
  // Fill score matrix, find best alignment, and return it.
  template<class ScoreMatrixType>
  Alignment make_best_alignment(const AlignTask<ScoreMatrixType>& task) {

    const AlignOpts& align_opts = *task.align_opts;

    // populate the score matrix
    fill_score_matrix(task);

    // get the best alignment.
    ScoreCellPVec trail;
    bool have_alignment = get_best_alignment_trail(task, trail);
    if (!have_alignment) {
      return INVALID_ALIGNMENT;
    }

    Alignment aln(alignment_from_trail(task, trail));
    if (align_opts.rescale_query) {
      aln.rescale_matched_chunks(align_opts);
    }
    return aln;

  }

  // Fill score matrix, find best alignment, and return it.
  template<class ScoreMatrixType>
  Alignment make_best_alignment_using_partials(const AlignTask<ScoreMatrixType>& task) {

    const AlignOpts& align_opts = *task.align_opts;

    // populate the score matrix
    fill_score_matrix_using_partials(task);

    // get the best alignment.
    ScoreCellPVec trail;
    bool have_alignment = get_best_alignment_trail(task, trail);
    if (!have_alignment) {
      return INVALID_ALIGNMENT;
    }

    Alignment aln(alignment_from_trail(task, trail));
    if (align_opts.rescale_query) {
      aln.rescale_matched_chunks(align_opts);
    }
    return aln;

  }

  //////////////////////////////////////////////////////////
  // Fill score matrix, find best n alignments, and return them in a vector..
  template<class ScoreMatrixType>
  int make_best_alignments_using_partials(const AlignTask<ScoreMatrixType>& task) {

    // Unpack the alignment task
    const IntVec& query = *task.query;
    const IntVec& ref = *task.ref;
    const AlignOpts& align_opts = *task.align_opts;

    // populate the score matrix
    fill_score_matrix_using_partials(task);

    // get the best alignments and store the results in the task.
    int num_alignments = get_best_alignments_try_all(task);
    return num_alignments;
  }

  template<class ScoreMatrixType>
  std::ostream& print_align_task(std::ostream& os, const AlignTask<ScoreMatrixType>& task) {

    os << "align_task:\n"
       << "\tquery: " << task.query << "\n"
       << "\tref: " << task.ref << "\n"
       << std::endl;

    return os;
  }

}

#endif