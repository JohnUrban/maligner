#include <vector>
#include <math.h>  
#include <algorithm>

#include "common_math.h"

// Compute the median absolute deviation
double mad(const std::vector<double>& in) {

  using std::size_t;
  if(in.empty()) return 0.0;

  std::vector<double> v(in);

  const size_t N = v.size();
  double m = median(v);

  for(size_t i = 0; i < N; i++) {
    v[i] = fabs(v[i] - m);
  }

  return median(v);

}


// Compute the median
double median(const std::vector<double>& in) {
  using std::size_t;
  if(in.empty()) return 0.0;
  std::vector<double> v(in);
  std::sort(v.begin(), v.end());

  const size_t N = v.size();
  
  if( N % 2 == 0) {
    const size_t mid_plus {N/2};
    return 0.5*(v[mid_plus - 1] + v[mid_plus]);
  }

  return v[size_t(N/2)];

}