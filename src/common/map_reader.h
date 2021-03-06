#ifndef MAPREADER_H
#define MAPREADER_H

#include <fstream>
#include "map.h"

namespace maligner_maps {
  
  class ReadMapException : public std::exception
  {
    public:

      ReadMapException(const std::string& line) {
        message_ = "Error reading map: " + line;
      };

      virtual const char* what() const noexcept
      {
        return message_.c_str();
      }

      std::string message_;

  };


  class MapReader {
  public:
    MapReader(const std::string& file_name) : f_(file_name) {};
    ~MapReader() { f_.close(); }
    bool next(Map& map);
    MapVec read_all_maps();

  private:
    std::ifstream f_;

  };

  MapVec read_maps(std::string& filename);

}

#endif