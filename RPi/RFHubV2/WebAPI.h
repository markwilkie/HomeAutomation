#include <curl/curl.h>

class WebAPI
{
public:
	//Constructor / destructor
	WebAPI(const std::string &url);  //constructor

	//GET and POST
	std::string PlainPOST(const std::string &controller,const std::string &api,const std::string &postdata); 
	std::string AuthenticatedPOST(const std::string &controller,const std::string &api,const std::string &postdata,const std::string &token);
	std::string PlainGET(const std::string &controller,const std::string &token);
	std::string AuthenticatedGET(const std::string &controller,const std::string &api,const std::string &token);

	//! true if HTTP error occurs (404 for example)
	bool isHTTPError() const;

	//! append received data into internal buffer by write_data_callback
	size_t append_data(void* ptr, size_t size, size_t nmemb);

private:
	//! no compiler-generated copy constructor
	WebAPI(const WebAPI &);

	std::string CallWebAPI(const std::string &controller,const std::string &api,const std::string &token,const std::string &postData, int type);

	std::string baseURL;
	std::string webResponse;
	long httpReturnCode;
};