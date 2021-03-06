/***
 * utilities for https
 */

#include "curl/curl.h"
/***
 * A structure to hold HTTP parameters.
 * NOTE: encoding is done within the get/put call
 */
struct Parameter {
	char* name;
	char* value;
};

struct HttpConnection {
	CURL* curl;
	struct curl_slist* headers;
	char* post_parameters;
	char* encoded_post_parameters;
};


struct HttpConnection* utils_https_new();
void utils_https_free(struct HttpConnection* connection);

/***
 * Send an HTTPS GET and receive the results
 * @param url the complete url (i.e. 'https://myserver.org/market/getmarket?pair=BTCUSD&nonce=....')
 * @param results where to store the results. NOTE: memory will be allocated
 * @returns 0 on success, otherwise a negative number that denotes the error
 */
int utils_https_get(struct HttpConnection* http_connection, const char* url, char** results);

int utils_https_post(struct HttpConnection* http_connection, const char* url, char** results);

void utils_https_add_post_parameter(struct HttpConnection* http_connection, const char* name, const char* value);

void utils_https_add_header(struct HttpConnection* http_connection, const char* name, const char* value);

char* utils_https_encode_parameters(struct HttpConnection* http_connection);

char* utils_https_get_nonce();
unsigned char* utils_https_bytes_to_hex_string(const unsigned char* bytes, size_t incoming_len, size_t* result_len);

/***
 * Sign a message using the HMAC-SHA512 method
 * @param message the message to sign
 * @returns a pointer to an allocated string that is the signed value
 */
unsigned char* utils_https_sign(const unsigned char* key, const unsigned char* message);
