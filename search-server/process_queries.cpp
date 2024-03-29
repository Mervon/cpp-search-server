#include "process_queries.h"
#include "document.h"
#include "search_server.h"

#include <numeric>
#include <vector>
#include <string>
#include <execution>

std::vector<std::vector<Document>> ProcessQueries(const SearchServer& search_server, const std::vector<std::string>& queries) {
    std::vector<std::vector<Document>> documents_lists(queries.size());
    
    transform(std::execution::par, queries.begin(), queries.end(), documents_lists.begin(), [&search_server](std::string s){
                                return search_server.FindTopDocuments(s);
                                });

    return documents_lists;
}

std::vector<Document> ProcessQueriesJoined(
    const SearchServer& search_server,
    const std::vector<std::string>& queries) {
    std::vector<Document> documents;
    std::vector<std::vector<Document>> processed_queries = ProcessQueries(search_server, queries);
    for (auto& query_row : processed_queries) {
        for (auto& item : query_row) {
            documents.push_back(item);
        }
    }
    return documents;
}
