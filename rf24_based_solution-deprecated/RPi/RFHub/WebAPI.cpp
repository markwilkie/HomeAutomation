#include <string>
#include <stdexcept>
#include <cstdlib>
#include <iostream> 
#include <curl/curl.h>

#include "Logger.h"
#include "Exception.h"
#include "WebAPI.h"

extern "C" {
    size_t WriteFunc(void *ptr, size_t size, size_t nmemb, void* pInstance);
}

//Constants
#define GET  0
#define POST 1

WebAPI::WebAPI(const std::string &url)
{
    if ( url.length() == 0 )
        throw std::runtime_error("URL can't be of zero length");
    baseURL  = url;
    httpReturnCode=0;
}

bool WebAPI::isHTTPError() const
{
    if (httpReturnCode < 200 || httpReturnCode > 300) {
        return true;
    }
    return false;
}

std::string WebAPI::PlainPOST(const std::string &controller,const std::string &api,const std::string &postData)
{
	LogLine() << "PostData: " << postData;
    return CallWebAPI(controller, api, "" , postData, POST);
}

std::string WebAPI::AuthenticatedPOST(const std::string &controller,const std::string &api,const std::string &postData,const std::string &token)
{
	LogLine() << "PostData: " << postData;
    return CallWebAPI(controller, api, token, postData, POST);
}

std::string WebAPI::PlainGET(const std::string &controller,const std::string &api)
{
	LogLine() << "GetData: " << api;
    return CallWebAPI(controller, api, "", "", GET);
}

std::string WebAPI::AuthenticatedGET(const std::string &controller,const std::string &api,const std::string &token)
{
	LogLine() << "GetData: " << api;
    return CallWebAPI(controller, api, token, "", GET);
}

std::string WebAPI::CallWebAPI(const std::string &controller,const std::string &api, const std::string &token,const std::string &postData, int type)
{
    //Init
    CURL *curl = curl_easy_init();
    if ( curl == NULL )
        throw std::runtime_error("Unable to initialize curl handler");
    CURLcode res;
    char errbuf[CURL_ERROR_SIZE];
    errbuf[0]=0;

    std::string url = baseURL + controller + api;
    //LogLine() << "URL: " << url;
    std::string tokenString = "Authorization: Bearer " + token;

    //set headers
    struct curl_slist *chunk = NULL;
    chunk = curl_slist_append(chunk, "Accept:");
    chunk = curl_slist_append(chunk, "Content-Type: application/json;type=entry;charset=utf-8");
    if(token.length()>2)
        chunk = curl_slist_append(chunk, tokenString.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, chunk);

    //Set options
    //curl_easy_setopt(curl, CURLOPT_VERBOSE, 1L);
    curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, errbuf);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30);  //set timeout for 30 seconds
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str()); //set URL
    if (type == POST)
    {
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, postData.c_str());
    }
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteFunc);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, this);

    //
    // Perform the request, res will get the return code 
    //
    res = curl_easy_perform(curl);
    if ( res != CURLE_OK)
    {
        LogErrorLine() << "Curl ERROR: " << (int)res << " " << curl_easy_strerror(res);
		LogErrorLine() << "Long(er) Error: " << errbuf;

        //Cleanup
        curl_easy_cleanup(curl);
        curl_slist_free_all(chunk);

        throw WebAPIException(0,(int)res,errbuf);  //CURL exception type
    }

    //Save return code
    long retCode;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &retCode);
    httpReturnCode=retCode;  //because of C calling

    //Cleanup
    curl_easy_cleanup(curl);
    curl_slist_free_all(chunk);

    if(isHTTPError())
    {
		LogErrorLine() << "Http Error Code: " << httpReturnCode;
		LogErrorLine() << "Raw Response: " << webResponse.c_str();
        throw WebAPIException(1,(int)httpReturnCode,"HTTP Error"); //http exception type
    }

    return webResponse;
}

size_t WebAPI::append_data(void* ptr, size_t size, size_t nmemb)
{
    size_t bytes = size * nmemb;
    const char *s = static_cast<char*>(ptr);
    webResponse += std::string(s, s + bytes);
    return bytes;
}

//http://stackoverflow.com/questions/2329571/c-libcurl-get-output-into-a-string
size_t WriteFunc(void *ptr, size_t size, size_t nmemb, void* pInstance)
{
    WebAPI* obj = reinterpret_cast<WebAPI*>(pInstance);
    return obj->append_data(ptr, size, nmemb);
}