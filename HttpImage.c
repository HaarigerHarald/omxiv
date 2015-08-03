/**
 * Modified from: http://curl.haxx.se/libcurl/c/getinmemory.html 
 * Copyright (C) 1998 - 2015, Daniel Stenberg, <daniel@haxx.se>, et al.
 * Distributed under: http://curl.haxx.se/docs/copyright.html
**/

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include <curl/curl.h>

struct MemoryStruct {
	char *memory;
	size_t size;
};

static size_t WriteMemoryCallback(void *contents, size_t size, size_t nmemb, void *userp)
{
	size_t realsize = size * nmemb;
	struct MemoryStruct *mem = (struct MemoryStruct *)userp;

	mem->memory = realloc(mem->memory, mem->size + realsize);
	if(!mem->memory) {
		return 0;
	}

	memcpy(&(mem->memory[mem->size]), contents, realsize);
	mem->size += realsize;

	return realsize;
}

char* getImageFromUrl(const char *url, size_t *size){
	CURL *curl_handle;
	CURLcode res;

	struct MemoryStruct chunk;

	chunk.memory = NULL;
	chunk.size = 0;

	curl_global_init(CURL_GLOBAL_ALL);

	curl_handle = curl_easy_init();

	curl_easy_setopt(curl_handle, CURLOPT_URL, url);

	curl_easy_setopt(curl_handle, CURLOPT_WRITEFUNCTION, WriteMemoryCallback);

	curl_easy_setopt(curl_handle, CURLOPT_WRITEDATA, (void *)&chunk);

	curl_easy_setopt(curl_handle, CURLOPT_USERAGENT, "libcurl-agent/1.0");

	res = curl_easy_perform(curl_handle);

	if(res != CURLE_OK) {
		free(chunk.memory);
		chunk.memory=NULL;
		chunk.size = 0;
		printf("libCurl returned error code %d\n", res);
	}
	
	*size= chunk.size;

	curl_easy_cleanup(curl_handle);
	curl_global_cleanup();
	
	return chunk.memory;
}
