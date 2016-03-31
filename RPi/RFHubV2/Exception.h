#include <stdexcept>

class WebAPIException: public std::runtime_error 
{
 public:

  int type;  //0=CURL, 1=http
  int errCode;
  std::string errString;

  WebAPIException ( int _type, int _errCode, const std::string &_errString ): runtime_error (_errString) 
  {
    type=_type;
    errCode=_errCode;
    errString=_errString;
  }

  ~WebAPIException() throw() {}
};
