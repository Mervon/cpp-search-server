#include "log_duration.h"
#include "search_server.h"

#include <algorithm>
#include <numeric>
#include <iostream>
#include <set>
#include <execution>

SearchServer::SearchServer(const std::string& stop_words_text)
    : SearchServer(SplitIntoWords(stop_words_text)){
}

SearchServer::SearchServer(std::string_view stop_words_text)
    : SearchServer(SplitIntoWords(stop_words_text)){
}

void SearchServer::AddDocument(int document_id, std::string_view document, DocumentStatus status, const std::vector<int>& ratings) {
    using namespace std::string_literals;

    if ((document_id < 0) || (documents_.count(document_id) > 0)) {
        throw std::invalid_argument("Invalid document_id"s);
    }

    const auto [it, inserted] = documents_.emplace(document_id, DocumentData{ComputeAverageRating(ratings), status, std::string(document)});
    
    const auto words = SplitIntoWordsNoStop(it->second.document_words);

    const double inv_word_count = 1.0 / words.size();

    for (std::string_view word : words) {
        word_to_document_freqs_[word][document_id] += inv_word_count;
        document_to_word_freqs_[document_id][word] += inv_word_count;
    }

    document_ids_.insert(document_id);
}

std::vector<Document> SearchServer::FindTopDocuments(std::string_view raw_query, DocumentStatus status) const {
    return FindTopDocuments(raw_query, [status](int document_id, DocumentStatus document_status, int rating) {
        return document_status == status;
    });
}

std::vector<Document> SearchServer::FindTopDocuments(std::string_view raw_query) const {
    return FindTopDocuments(raw_query, DocumentStatus::ACTUAL);
}

int SearchServer::GetDocumentCount() const {
    return documents_.size();
}

bool SearchServer::IsStopWord(std::string_view word) const {
    return stop_words_.count(word) > 0;
}

bool SearchServer::IsValidWord(std::string_view word) {
    // A valid word must not contain special characters
    return std::none_of(word.begin(), word.end(), [](char c) {
        return c >= '\0' && c < ' ';
    });
}

std::vector<std::string_view> SearchServer::SplitIntoWordsNoStop(std::string_view text) const {
    using namespace std::string_literals;

    std::vector<std::string_view> words;

    for (std::string_view word : SplitIntoWords(text)) {
        if (!IsValidWord(word)) {
            throw std::invalid_argument("Word "s + std::string(word) + " is invalid"s);
        }

        if (!IsStopWord(word)) {
            words.push_back(word);
        }
    }

    return words;
}

int SearchServer::ComputeAverageRating(const std::vector<int>& ratings) {
    if (ratings.empty()) {
        return 0;
    }

    int rating_sum = std::accumulate(ratings.begin(), ratings.end(), 0);

    return rating_sum / static_cast<int>(ratings.size());
}

struct SearchServer::QueryWord {
    std::string_view data;
    bool is_minus;
    bool is_stop;
};

SearchServer::QueryWord SearchServer::ParseQueryWord(std::string_view word) const {
    using namespace std::string_literals;

    if (word.empty()) {
        throw std::invalid_argument("Query word is empty"s);
    }

    bool is_minus = false;

    if (word[0] == '-') {
        is_minus = true;
        word = word.substr(1);
    }

    if (word.empty() || word[0] == '-' || !IsValidWord(word)) {
        throw std::invalid_argument("Query word "s + std::string(word) + " is invalid"s);
    }

    return {word, is_minus, IsStopWord(word)};
}

double SearchServer::ComputeWordInverseDocumentFreq(std::string_view word) const {
    return log(GetDocumentCount() * 1.0 / word_to_document_freqs_.at(word).size());
}

std::set<int>::iterator SearchServer::begin() {
    return(document_ids_.begin());
}

std::set<int>::iterator SearchServer::end() {
    return(document_ids_.end());
}

const std::map<std::string_view, double>& SearchServer::GetWordFrequencies(int document_id) const {
    static const std::map<std::string_view, double> dummy;

    bool check_for_existing_id = binary_search(document_ids_.begin(), document_ids_.end(), document_id);

    if (check_for_existing_id) {
        return (document_to_word_freqs_.at(document_id));
    }

    return (dummy);
}

std::tuple<std::vector<std::string_view>, DocumentStatus> SearchServer::MatchDocument(const std::string_view raw_query, int document_id) const {
    return MatchDocument(std::execution::seq, raw_query, document_id);
}

std::tuple<std::vector<std::string_view>, DocumentStatus> SearchServer::MatchDocument(const std::execution::parallel_policy&, std::string_view raw_query, int document_id) const {
    using namespace std::string_literals;

    const auto query = ParseQueryExecPol(raw_query);

    std::vector<std::string_view> matched_words(query.plus_words.size());

    if (std::any_of(std::execution::par, query.minus_words.begin(), query.minus_words.end(),
            [this, document_id](std::string_view word) {
                return document_to_word_freqs_.at(document_id).count(word);
            })) { 
        return { {}, documents_.at(document_id).status }; 
    }

    std::copy_if(std::execution::par, std::make_move_iterator(query.plus_words.begin()), std::make_move_iterator(query.plus_words.end()),
            matched_words.begin(),
            [this, document_id](std::string_view word) {
                return (document_to_word_freqs_.at(document_id).count(word));
        });

    std::sort(std::execution::par, matched_words.begin(), matched_words.end());
    auto it = std::unique(std::execution::par, matched_words.begin(), matched_words.end());

    return { {matched_words.begin(), it - 1}, SearchServer::documents_.at(document_id).status };
}

std::tuple<std::vector<std::string_view>, DocumentStatus> SearchServer::MatchDocument(const std::execution::sequenced_policy&, std::string_view raw_query, int document_id) const {
    using namespace std::string_literals;

    const auto query = ParseQuery(raw_query);

    std::vector<std::string_view> matched_words;

    for (std::string_view word : query.minus_words) {
        if (word_to_document_freqs_.count(word) == 0) {
            continue;
        }

        if (word_to_document_freqs_.at(word).count(document_id)) {
            return { {}, documents_.at(document_id).status};
        }
    }

    for (std::string_view word : query.plus_words) {
        if (word_to_document_freqs_.count(word) == 0) {
            continue;
        }

        if (word_to_document_freqs_.at(word).count(document_id)) {
            matched_words.push_back(word);
        }
    }

    return {matched_words, documents_.at(document_id).status};
}

SearchServer::Query SearchServer::ParseQueryExecPol(std::string_view text) const {
    Query result;
    std::vector<std::string_view> buff_text = SplitIntoWords(text);

    for (std::string_view word : buff_text) {
        const auto query_word = ParseQueryWord(word); //move()

        if (!query_word.is_stop) {
            if (query_word.is_minus) {
                result.minus_words.push_back(std::move(query_word.data));
            } else {
                result.plus_words.push_back(std::move(query_word.data));
            }
        }
    }

    return result;
}

SearchServer::Query SearchServer::ParseQuery(std::string_view text) const {
    Query result;
    std::vector<std::string_view> buff_text = SplitIntoWords(text);

    for (std::string_view word: buff_text) {
        const auto query_word = ParseQueryWord(word);

        if (!query_word.is_stop) {
            if (query_word.is_minus) {
                result.minus_words.push_back(query_word.data);
            } else {
                result.plus_words.push_back(query_word.data);
            }
        }
    }

    std::sort(result.minus_words.begin(), result.minus_words.end());
    auto it = std::unique(result.minus_words.begin(), result.minus_words.end());
    result.minus_words.resize(std::distance(result.minus_words.begin(), it));

    std::sort(result.plus_words.begin(), result.plus_words.end());
    it = std::unique(result.plus_words.begin(), result.plus_words.end());
    result.plus_words.resize(std::distance(result.plus_words.begin(), it)); 

    return result;
}

void SearchServer::RemoveDocument(int document_id) {
    RemoveDocument(std::execution::seq, document_id);
}

void SearchServer::RemoveDocument(const std::execution::sequenced_policy&, int document_id) {
        if (document_ids_.count(document_id)) {
            const std::map<std::string_view, double>& word_freqs = document_to_word_freqs_.at(document_id);
            
            std::vector<std::string_view> words(word_freqs.size());
            
            std::transform(std::execution::seq, word_freqs.begin(), word_freqs.end(), words.begin(),
                [](const std::pair<std::string_view, double>& item) {
                    return item.first;
                });

            std::for_each(std::execution::seq, words.begin(), words.end(),
                [this, document_id](std::string_view word) {
                    word_to_document_freqs_.at(word).erase(document_id);
                });
                
            documents_.erase(document_id);
            
            document_ids_.erase(document_id);
    }
}

void SearchServer::RemoveDocument(const std::execution::parallel_policy&, int document_id) {
        if (document_ids_.count(document_id)) {
            const std::map<std::string_view, double>& word_freqs = document_to_word_freqs_.at(document_id);
            
            std::vector<std::string> words(word_freqs.size());
            
            std::transform(std::execution::par, word_freqs.begin(), word_freqs.end(), words.begin(),
                [](const std::pair<std::string_view, double>& item) {
                    return item.first;
                });

            std::for_each(std::execution::par, words.begin(), words.end(),
                [this, document_id](std::string_view word) {
                    word_to_document_freqs_.at(word).erase(document_id);
                });
                
            documents_.erase(document_id);
            
            document_ids_.erase(document_id);
    }
}








