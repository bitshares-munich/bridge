/***
 * All of the connectivity for https://www.bittrex.com
 */
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "bridge/book.h"
#include "utils/https.h"
#include "utils/json.h"

const char* bittrex_url = "https://bittrex.com/api/v1.1/";
// TODO: these are test values. They need to be replaced
const char* bittrex_apikey = "55bbbe56a47046ac858f26110b044b6d";
const char* bittrex_apisecret = "5d7773773d5c481f9ec1afa67227a7b4";


/***
 * Build a URL compatible with bittrex
 * @param group public, account, or market
 * @param method the method to call
 * @param nonce the calculated nonce
 * @param results where to put the results
 * @returns the resultant string (that is also in the results parameter)
 */
char* bittrex_build_url(const char* group, const char* method, const char* nonce, char** results) {
	int len = strlen(bittrex_url) + strlen(group) + strlen(method) + strlen(bittrex_apikey) + 20;
	if (nonce != NULL)
		len += strlen(nonce);
	*results = malloc(len);
	if (*results) {
		if (strcmp(group, "public") == 0) // public methods don't require apikey or nonce
			snprintf(*results, len, "%s%s/%s", bittrex_url, group, method);
		else
			snprintf(*results, len, "%s%s/%s?apikey=%s&nonce=%s", bittrex_url, group, method, bittrex_apikey, nonce);
	}
	return *results;
}

struct Market* book_bittrex_parse_market(const char* json) {
	struct Market* head = NULL;
	struct Market* current = head;
	struct Market* last = head;
	jsmntok_t tokens[65535];

	if (json == NULL)
		return NULL;

	// parse json
	int total_tokens = json_parse(json, tokens, 65535);
	if (total_tokens < 0)
		return NULL;
	// first make sure it was a success
	int success = 0;
	int start_pos = 0;
	start_pos = json_get_int_value(json, tokens, total_tokens, start_pos, "success", &success);
	if (start_pos == 0 || !success)
		return NULL;
	// loop through markets
	while (start_pos != 0) {
		current = market_new();
		if (current == NULL) {
			free(head);
			return NULL;
		}
		start_pos = json_get_string_value(json, tokens, total_tokens, start_pos, "MarketCurrency", &current->market_currency);
		if (start_pos == 0) {
			free(current);
			continue;
		}
		start_pos = json_get_string_value(json, tokens, total_tokens, start_pos, "BaseCurrency", &current->base_currency);
		if (start_pos == 0) {
			free(current->market_currency);
			free(current);
			continue;
		}
		start_pos = json_get_double_value(json, tokens, total_tokens, start_pos, "MinTradeSize", &current->min_trade_size);
		if (start_pos == 0) {
			free(current->market_currency);
			free(current->base_currency);
			free(current);
			continue;
		}
		start_pos = json_get_string_value(json, tokens, total_tokens, start_pos, "MarketName", &current->market_name);
		if (start_pos == 0) {
			free(current->market_currency);
			free(current->base_currency);
			free(current);
			continue;
		}
		// don't add if it is not active
		int active;
		start_pos = json_get_int_value(json, tokens, total_tokens, start_pos, "IsActive", &active);
		if (start_pos == 0 || !active ) {
			free(current->market_currency);
			free(current->base_currency);
			free(current->market_name);
			free(current);
			continue;
		}
		current->fee = 0.7;
		// add it to the list
		if (head == NULL) {
			head = current;
		} else {
			last->next = current;
		}
		last = current;
	}
	return head;
}

struct Book* book_bittrex_parse_book(const char* json) {
	struct Book* book = NULL;
	struct Book* current = book;
	struct Book* last = book;
	jsmntok_t tokens[65535];

	if (json == NULL) {
		return NULL;
	}
	// parse json
	int tok_no = json_parse(json, tokens, 65535);
	if (tok_no < 0)
		return NULL;
	// first make sure it was a success
	int success = 0;
	int start_pos = 0;
	start_pos = json_get_int_value(json, tokens, tok_no, start_pos, "success", &success);
	if ( start_pos == 0 || !success)
		return NULL;
	// find where is the "sell"
	int sell_pos = json_find_token(json, tokens, tok_no, start_pos, "sell");
	// start building the book
	// loop for the bid
	while (start_pos != 0 && start_pos < sell_pos) {
		double quantity = 0;
		double rate = 0;
		start_pos = json_get_double_value(json, tokens, tok_no, start_pos, "Quantity", &quantity);
		if (start_pos == 0)
			continue;
		start_pos = json_get_double_value(json, tokens, tok_no, start_pos, "Rate", &rate);
		if (start_pos == 0)
			continue;
		if (start_pos < sell_pos) {
			current = book_new();
			current->bid_qty = quantity;
			current->bid_price = rate;
			if (book == NULL) {
				book = current;
				last = current;
			} else {
				last->next = current;
			}
			last = current;
		}
	}
	// loop for the ask
	start_pos = sell_pos;
	current = book;
	while (start_pos != 0) {
		double quantity = 0;
		double rate = 0;
		start_pos = json_get_double_value(json, tokens, tok_no, start_pos, "Quantity", &quantity);
		if (start_pos == 0)
			continue;
		start_pos = json_get_double_value(json, tokens, tok_no, start_pos, "Rate", &rate);
		if (start_pos == 0)
			continue;
		if (current == NULL) {
			// need to add new Book to list
			current = book_new();
			if (last == NULL) {
				// we don't have "book" yet
				book = current;
			} else {
				last->next = current;
			}
		}
		current->ask_qty = quantity;
		current->ask_price = rate;
		last = current;
		current = current->next;
	} // end of while loop
	return book;
}

/***
 * parse JSON to get balance of a currency
 */
struct Balance* bittrex_parse_balance(const char* json) {
	struct Balance* retVal = NULL;
	jsmntok_t tokens[1000];
	int start_pos = 0;
	int success = 0;
	int token_position = 0;

	// parse json
	int num_tokens = json_parse(json, tokens, 1000);
	if (num_tokens < 0)
		goto exit;;

	retVal = balance_new();
	if (retVal == NULL)
		goto exit;

	token_position = json_find_token(json, tokens, num_tokens, start_pos, "Currency");
	if (token_position < 0)
		goto exit;
	json_get_string(json, tokens[++token_position], &retVal->currency);

	token_position = json_find_token(json, tokens, num_tokens, token_position, "Balance");
	if (token_position < 0)
		goto exit;
	json_get_double(json, tokens[++token_position], &retVal->balance);

	token_position = json_find_token(json, tokens, num_tokens, token_position, "Available");
	if (token_position < 0)
		goto exit;
	json_get_double(json, tokens[++token_position], &retVal->available);

	token_position = json_find_token(json, tokens, num_tokens, token_position, "Pending");
	if (token_position < 0)
		goto exit;
	json_get_double(json, tokens[++token_position], &retVal->pending);
	success = 1;
	exit:
	if (!success) {
		if (retVal != NULL) {
			free(retVal);
			retVal = NULL;
		}
	}
	return retVal;
}

struct Book* bittrex_get_books(const struct Market* market) {
	char getorderbook[50];
	snprintf(getorderbook, sizeof(getorderbook), "getorderbook?market=%s-%s&type=%s&depth=50", market->base_currency, market->market_currency, "both");
	char* url;
	bittrex_build_url("public", getorderbook, NULL, &url);
	char* results;
	struct HttpConnection* connection = utils_https_new();
	utils_https_get(connection, url, &results);
	free(url);
	utils_https_free(connection);
	struct Book* book = book_bittrex_parse_book(results);
	free(results);
	return book;
}

struct Market* bittrex_get_markets() {
	char* url;
	bittrex_build_url("public", "getmarkets", NULL, &url);
	char* results;
	struct HttpConnection* connection = utils_https_new();
	utils_https_get(connection, url, &results);
	utils_https_free(connection);
	free(url);
	struct Market* market = book_bittrex_parse_market(results);
	free(results);
	return market;
}

int bittrex_limit_buy(const struct Market* currencyPair, double rate, double quantity) {
	return 0;
}

int bittrex_limit_sell(const struct Market* currencyPair, double rate, double quantity) {
	return 0;
}

int bittrex_market_buy(const struct Market* currencyPair, double quantity) {
	return 0;
}

int bittrex_market_sell(const struct Market* currencyPair, double quantity) {
	return 0;
}

struct Balance* bittrex_balance(const char* currency) {
	const char* template = "%saccount/getbalance?apikey=%s&nonce=%s&currency=%s";
	char* nonce = utils_https_get_nonce();
	int url_len = strlen(bittrex_url) + strlen(template) + strlen(bittrex_apikey) + strlen(currency) + strlen(nonce) - 2;
	char url[url_len];
	snprintf(url, sizeof(url), template, bittrex_url, bittrex_apikey, nonce, currency);
	free(nonce);
	unsigned char* signature = utils_https_sign((unsigned char*)bittrex_apisecret, (unsigned char*)url);
	char* json;
	struct HttpConnection* connection = utils_https_new();
	utils_https_add_header(connection, "apisign", (char*)signature);
	utils_https_get(connection, url, &json);
	utils_https_free(connection);
	free(signature);
	struct Balance* retVal = bittrex_parse_balance(json);
	if (retVal == NULL) {
		fprintf(stderr, "Error retrieving balance for %s on Bittrex.com: %s\n", currency, json);
	}
	free(json);
	return retVal;
}


