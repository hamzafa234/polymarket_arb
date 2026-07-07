// orderbook.cpp
//
// Fetches order book data from the Polymarket CLOB REST API and prints
// it to the terminal. Optionally polls on an interval so you get a
// live-updating view.
//
// Endpoint used (public, no auth required):
//   GET https://clob.polymarket.com/book?token_id=<TOKEN_ID>
//
// Response shape:
// {
//   "market": "0x...",
//   "asset_id": "0x...",
//   "timestamp": "1234567890",
//   "hash": "...",
//   "bids": [ { "price": "0.45", "size": "100" }, ... ],
//   "asks": [ { "price": "0.46", "size": "150" }, ... ],
//   "min_order_size": "1",
//   "tick_size": "0.01",
//   "neg_risk": false,
//   "last_trade_price": "0.45"
// }
//
// Build (see README.md for dependency install instructions):
//   g++ -std=c++17 orderbook.cpp -o orderbook -lcurl
//
// Run:
//   ./orderbook <token_id> [refresh_seconds]
//
//   token_id          The CLOB token ID for the outcome you want to watch.
//                      (Get this from the Gamma API's "clobTokenIds" field
//                      for a market, e.g. via
//                      https://gamma-api.polymarket.com/markets?slug=<slug>)
//   refresh_seconds    Optional. If provided, the program polls the book
//                      on this interval instead of fetching once and exiting.

#include <curl/curl.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

using json = nlohmann::json;

namespace {

constexpr const char* kClobBaseUrl = "https://clob.polymarket.com";

// libcurl write callback: appends received bytes into a std::string.
size_t WriteCallback(char* contents, size_t size, size_t nmemb, void* userp) {
    size_t total = size * nmemb;
    static_cast<std::string*>(userp)->append(contents, total);
    return total;
}

// Performs a simple HTTP GET and returns the response body.
// Throws std::runtime_error on transport-level failure or non-2xx status.
std::string HttpGet(const std::string& url) {
    CURL* curl = curl_easy_init();
    if (!curl) {
        throw std::runtime_error("Failed to initialize curl");
    }

    std::string response;
    long http_code = 0;

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "polymarket-orderbook-cpp/1.0");

    CURLcode res = curl_easy_perform(curl);
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK) {
        throw std::runtime_error(std::string("curl error: ") + curl_easy_strerror(res));
    }
    if (http_code < 200 || http_code >= 300) {
        throw std::runtime_error("HTTP " + std::to_string(http_code) + ": " + response);
    }
    return response;
}

struct Level {
    double price = 0.0;
    double size = 0.0;
};

struct OrderBook {
    std::string market;
    std::string asset_id;
    std::string last_trade_price;
    std::string tick_size;
    std::vector<Level> bids;
    std::vector<Level> asks;
};

double ToDouble(const json& j) {
    // The API returns price/size as strings; handle numbers too, just in case.
    if (j.is_string()) return std::stod(j.get<std::string>());
    if (j.is_number()) return j.get<double>();
    return 0.0;
}

OrderBook ParseOrderBook(const json& j) {
    OrderBook book;
    book.market = j.value("market", "");
    book.asset_id = j.value("asset_id", "");
    book.last_trade_price = j.value("last_trade_price", "");
    book.tick_size = j.value("tick_size", "");

    for (const auto& lvl : j.value("bids", json::array())) {
        book.bids.push_back({ToDouble(lvl.at("price")), ToDouble(lvl.at("size"))});
    }
    for (const auto& lvl : j.value("asks", json::array())) {
        book.asks.push_back({ToDouble(lvl.at("price")), ToDouble(lvl.at("size"))});
    }

    // Polymarket doesn't guarantee ordering, so sort explicitly:
    // bids highest price first, asks lowest price first.
    std::sort(book.bids.begin(), book.bids.end(),
              [](const Level& a, const Level& b) { return a.price > b.price; });
    std::sort(book.asks.begin(), book.asks.end(),
              [](const Level& a, const Level& b) { return a.price < b.price; });

    return book;
}

void PrintOrderBook(const OrderBook& book, int depth) {
    std::cout << std::fixed << std::setprecision(4);

    std::cout << "Market:   " << book.market << "\n";
    std::cout << "Asset ID: " << book.asset_id << "\n";
    if (!book.last_trade_price.empty()) {
        std::cout << "Last trade price: " << book.last_trade_price << "\n";
    }
    std::cout << "\n";

    // Best bid / ask / spread summary
    if (!book.bids.empty() && !book.asks.empty()) {
        double best_bid = book.bids.front().price;
        double best_ask = book.asks.front().price;
        std::cout << "Best bid: " << best_bid
                  << "   Best ask: " << best_ask
                  << "   Spread: " << (best_ask - best_bid) << "\n\n";
    }

    std::cout << std::setw(12) << "BID SIZE" << std::setw(10) << "PRICE"
               << "   |   " << std::setw(10) << "PRICE" << std::setw(12) << "ASK SIZE" << "\n";
    std::cout << std::string(56, '-') << "\n";

    size_t rows = static_cast<size_t>(depth);
    size_t max_rows = std::max(book.bids.size(), book.asks.size());
    rows = std::min(rows, max_rows);

    for (size_t i = 0; i < rows; ++i) {
        if (i < book.bids.size()) {
            std::cout << std::setw(12) << book.bids[i].size << std::setw(10) << book.bids[i].price;
        } else {
            std::cout << std::setw(12) << "" << std::setw(10) << "";
        }

        std::cout << "   |   ";

        if (i < book.asks.size()) {
            std::cout << std::setw(10) << book.asks[i].price << std::setw(12) << book.asks[i].size;
        } else {
            std::cout << std::setw(10) << "" << std::setw(12) << "";
        }
        std::cout << "\n";
    }
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <token_id> [refresh_seconds] [depth]\n";
        std::cerr << "  token_id         CLOB token ID for the market outcome to watch\n";
        std::cerr << "  refresh_seconds  Optional. Poll on this interval (e.g. 3). Omit to fetch once.\n";
        std::cerr << "  depth            Optional. Number of price levels to show per side (default 10).\n";
        return 1;
    }

    std::string token_id = argv[1];
    int refresh_seconds = (argc >= 3) ? std::atoi(argv[2]) : 0;
    int depth = (argc >= 4) ? std::atoi(argv[3]) : 10;

    curl_global_init(CURL_GLOBAL_DEFAULT);

    std::string url = std::string(kClobBaseUrl) + "/book?token_id=" + token_id;

    do {
        try {
            std::string body = HttpGet(url);
            json parsed = json::parse(body);
            OrderBook book = ParseOrderBook(parsed);

            if (refresh_seconds > 0) {
                // Clear terminal between refreshes (ANSI escape codes).
                std::cout << "\033[2J\033[1;1H";
            }

            PrintOrderBook(book, depth);
        } catch (const std::exception& ex) {
            std::cerr << "Error fetching order book: " << ex.what() << "\n";
        }

        if (refresh_seconds > 0) {
            std::this_thread::sleep_for(std::chrono::seconds(refresh_seconds));
        }
    } while (refresh_seconds > 0);

    curl_global_cleanup();
    return 0;
}
