#include <stdio.h>
#include <string.h>
#include <curl/curl.h>
#include <sys/resource.h> 

class WebAPI 
{
	//Constants
	const int GET = 0;
	const int POST = 1;

	//State
	bool authenticatedFlag = FALSE;  //this will be set IF

	//Site specifics
	string baseURL; //base URL for web api call
	string pat;     //secret string to authenticate with
	string key;     //access token - used for authorization 

	//Data
	string webResponse="";  //response from web api call

	//Curl stuff
	CURL *curl;
	CURLcode res;
	struct ResultChunk 
	{
		char *ptr;
		size_t len;
	};

public:
	WebAPI(string, string);  //constructor
	//~WebAPI();               //destructor
	string Post(string, string, string); //controller, api, postdata
	string Get(string, string);
	string GetResponse();

private:
	void CallWebAPI(string,string,string,int);
	void CallWebAPI(string, string, int);
	string GetKey(char *);
};

void WebAPI::WebAPI(string _baseURL,string _key) 
{
	baseURL = _baseURL;
	key = _key;
}

//void WebAPI::~WebAPI;
//{
//	delete result;
//}

string GetResponse()
{
	return webResponse;
}

void Post(string controller, string api, string postData)
{
	CallWebAPI(controller, api, postData, POST);
}

void Get(string controller, string api)
{
	CallWebAPI(controller, api, GET);
}

void CallWebAPI(string controller, string api, int type)
{
	CallWebAPI(controller, api, "", type);
}

string GetKey()
{
	return "magic key that I parsed";
}

void CallWebAPI(string controller, string api, string postData, int type)
{
	struct ResultChunk result;
	InitResultChunk(&result);

	curl = curl_easy_init();
	if (curl)
	{
		//set headers
		struct curl_slist *chunk = NULL;
		chunk = curl_slist_append(chunk, "Accept:");
		chunk = curl_slist_append(chunk, "Content-Type: application/json;type=entry;charset=utf-8");
		if(authenticatedFlag)
			chunk = curl_slist_append(chunk, "Authorization: Bearer " + key);
		res = curl_easy_setopt(curl, CURLOPT_HTTPHEADER, chunk);

		//Set options
		//curl_easy_setopt(curl, CURLOPT_VERBOSE, 1L);
		curl_easy_setopt(curl, CURLOPT_TIMEOUT, 15);  //set timeout for 15 seconds
		curl_easy_setopt(curl, CURLOPT_URL, baseURL + controller + api); //set URL
		curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteFunc);
		curl_easy_setopt(curl, CURLOPT_WRITEDATA, &result);
		if (type == POST)
			curl_easy_setopt(curl, CURLOPT_POSTFIELDS, postData);

		/* Perform the request, res will get the return code */
		res = curl_easy_perform(curl);

		/* Check for errors */
		//
		// https://curl.haxx.se/libcurl/c/libcurl-errors.html
		//
		if (res != CURLE_OK)
		{
			//timeStamp(stderr);
			//fprintf(stderr, "ERROR: curl_easy_perform() failed: %s\n", curl_easy_strerror(res));
			//fflush(stderr);
		}
		else
		{
			//Set response to result from web call
			webResponse = result.ptr;
		}

		//Free result
		free(result.ptr);

		/* always cleanup */
		curl_easy_cleanup(curl);
		curl_slist_free_all(chunk);
	}

	void InitResultChunk(struct ResultChunk *s)
	{
		s->len = 0;
		s->ptr = malloc(s->len + 1);
		if (s->ptr == NULL) {
			fprintf(stderr, "malloc() failed\n");
			exit(EXIT_FAILURE);
		}
		s->ptr[0] = '\0';
	}

	//http://stackoverflow.com/questions/2329571/c-libcurl-get-output-into-a-string
	size_t WriteFunc(void *ptr, size_t size, size_t nmemb, struct ResultChunk *s)
	{
		size_t new_len = s->len + size*nmemb;
		s->ptr = realloc(s->ptr, new_len + 1);
		if (s->ptr == NULL) {
			fprintf(stderr, "realloc() failed\n");
			exit(EXIT_FAILURE);
		}
		memcpy(s->ptr + s->len, ptr, size*nmemb);
		s->ptr[new_len] = '\0';
		s->len = new_len;

		return size*nmemb;
	}
}