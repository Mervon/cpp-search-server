#pragma once

#include <algorithm>
#include <cmath>
#include <iostream>
#include <map>
#include <set>
#include <string>
#include <vector>
#include <execution>

#include "concurrent_map.h"
#include "document.h"
#include "log_duration.h"
#include "string_processing.h"

#define EPSILON 1e-6

const int MAX_RESULT_DOCUMENT_COUNT = 5;

class SearchServer {
public:
    template <typename StringContainer>
    explicit SearchServer(const StringContainer& stop_words);

    explicit SearchServer(const std::string& stop_words_text);

    explicit SearchServer(std::string_view stop_words_text);

    void AddDocument(int document_id, std::string_view document, DocumentStatus status, const std::vector<int>& ratings);

    template <typename DocumentPredicate>
    std::vector<Document> FindTopDocuments(std::string_view raw_query, DocumentPredicate document_predicate) const;
    std::vector<Document> FindTopDocuments(std::string_view raw_query, DocumentStatus status) const ;
    std::vector<Document> FindTopDocuments(std::string_view raw_query) const ;

    template <typename DocumentPredicate, typename ExecusionPolicy>
    std::vector<Document> FindTopDocuments(ExecusionPolicy&& policy, std::string_view raw_query, DocumentPredicate document_predicate) const;
    template <typename ExecusionPolicy>
    std::vector<Document> FindTopDocuments(ExecusionPolicy&& policy, std::string_view raw_query, DocumentStatus status) const ;
    template <typename ExecusionPolicy>
    std::vector<Document> FindTopDocuments(ExecusionPolicy&& policy, std::string_view raw_query) const ;

    int GetDocumentCount() const;

    std::tuple<std::vector<std::string_view>, DocumentStatus> MatchDocument(std::string_view raw_query, int document_id) const;
    std::tuple<std::vector<std::string_view>, DocumentStatus> MatchDocument(const std::execution::sequenced_policy&, std::string_view raw_query, int document_id) const;
    std::tuple<std::vector<std::string_view>, DocumentStatus> MatchDocument(const std::execution::parallel_policy&, std::string_view raw_query, int document_id) const;
    
    std::set<int>::iterator begin();

    std::set<int>::iterator end();

    const std::map<std::string_view, double>& GetWordFrequencies(int document_id) const;
    
    void RemoveDocument(int document_id);
    void RemoveDocument(const std::execution::sequenced_policy&, int document_id);
    void RemoveDocument(const std::execution::parallel_policy&, int document_id);

private:
    struct DocumentData {
        int rating;
        DocumentStatus status;
        std::string document_words;
    };
    const std::set<std::string, std::less<>> stop_words_;
    std::map<std::string_view, std::map<int, double>> word_to_document_freqs_;
    std::map<int, std::map<std::string_view, double>> document_to_word_freqs_;
    std::map<int, DocumentData> documents_;
    std::set<int> document_ids_;

    bool IsStopWord(std::string_view word) const ;

    static bool IsValidWord(std::string_view word) ;

    std::vector<std::string_view> SplitIntoWordsNoStop(std::string_view text) const ;

    static int ComputeAverageRating(const std::vector<int>& ratings) ;

    struct QueryWord;

    QueryWord ParseQueryWord(std::string_view text) const ;

    struct Query {
        std::vector<std::string_view> plus_words;
        std::vector<std::string_view> minus_words;
    };

    Query ParseQuery(const std::string& text) const;
    Query ParseQuery(std::string_view text) const;
    Query ParseQueryExecPol(std::string_view text) const;

    // Existence required
    double ComputeWordInverseDocumentFreq(std::string_view word) const ;

    template <typename DocumentPredicate>
    std::vector<Document> FindAllDocuments(const Query& query, DocumentPredicate document_predicate) const;
    template <typename DocumentPredicate, typename ExecusionPolicy>
    std::vector<Document> FindAllDocuments(ExecusionPolicy&& policy, const Query& query, DocumentPredicate document_predicate) const;
};

template <typename StringContainer>
    SearchServer::SearchServer(const StringContainer& stop_words)
        : stop_words_(MakeUniqueNonEmptyStrings(stop_words)){
        using namespace std::string_literals;
        if (!all_of(stop_words_.begin(), stop_words_.end(), IsValidWord)) {
            throw std::invalid_argument("Some of stop words are invalid"s);
        }
    }

template <typename DocumentPredicate>
std::vector<Document> SearchServer::FindTopDocuments(std::string_view raw_query, DocumentPredicate document_predicate) const {
    using namespace std::string_literals;

    const auto query = ParseQuery(raw_query);

    auto matched_documents = FindAllDocuments(query, document_predicate);

    sort(matched_documents.begin(), matched_documents.end(), [](const Document& lhs, const Document& rhs) {
        if (std::abs(lhs.relevance - rhs.relevance) < EPSILON) {
            return lhs.rating > rhs.rating;
        } else {
            return lhs.relevance > rhs.relevance;
        }
    });

    if (matched_documents.size() > MAX_RESULT_DOCUMENT_COUNT) {
        matched_documents.resize(MAX_RESULT_DOCUMENT_COUNT);
    }

    return matched_documents;
}

template <typename DocumentPredicate>
std::vector<Document> SearchServer::FindAllDocuments(const Query& query, DocumentPredicate document_predicate) const {
    std::map<int, double> document_to_relevance;
    for (std::string_view word : query.plus_words) {
        if (word_to_document_freqs_.count(word) == 0) {
            continue;
        }

        const double inverse_document_freq = ComputeWordInverseDocumentFreq(word);

        for (const auto [document_id, term_freq] : word_to_document_freqs_.at(word)) {
            const auto& document_data = documents_.at(document_id);

            if (document_predicate(document_id, document_data.status, document_data.rating)) {
                document_to_relevance[document_id] += term_freq * inverse_document_freq;
            }
        }
    }

    for (std::string_view word : query.minus_words) {
        if (word_to_document_freqs_.count(word) == 0) {
            continue;
        }

        for (const auto [document_id, _] : word_to_document_freqs_.at(word)) {
            document_to_relevance.erase(document_id);
        }
    }

    std::vector<Document> matched_documents;

    for (const auto [document_id, relevance] : document_to_relevance) {
        matched_documents.push_back({document_id, relevance, documents_.at(document_id).rating});
    }
    
    return matched_documents;
}

template <typename DocumentPredicate, typename ExecusionPolicy>
    std::vector<Document> SearchServer::FindTopDocuments(ExecusionPolicy&& policy, std::string_view raw_query, DocumentPredicate document_predicate) const {
    using namespace std::string_literals;

    const auto query = ParseQuery(raw_query);

    auto matched_documents = FindAllDocuments(policy, query, document_predicate);

    sort(matched_documents.begin(), matched_documents.end(), [](const Document& lhs, const Document& rhs) {
        if (std::abs(lhs.relevance - rhs.relevance) < EPSILON) {
            return lhs.rating > rhs.rating;
        } else {
            return lhs.relevance > rhs.relevance;
        }
    });

    if (matched_documents.size() > MAX_RESULT_DOCUMENT_COUNT) {
        matched_documents.resize(MAX_RESULT_DOCUMENT_COUNT);
    }

    return matched_documents;
}

template <typename ExecusionPolicy>
std::vector<Document> SearchServer::FindTopDocuments(ExecusionPolicy&& policy, std::string_view raw_query, DocumentStatus status) const {
    return FindTopDocuments(policy, raw_query, [status](int document_id, DocumentStatus document_status, int rating) {
        return document_status == status;
    });
}

template <typename ExecusionPolicy>
std::vector<Document> SearchServer::FindTopDocuments(ExecusionPolicy&& policy, std::string_view raw_query) const {
    return FindTopDocuments(policy, raw_query, DocumentStatus::ACTUAL);
}

template <typename DocumentPredicate, typename ExecusionPolicy>
std::vector<Document> SearchServer::FindAllDocuments(ExecusionPolicy&& policy, const Query& query, DocumentPredicate document_predicate) const {
    ConcurrentMap<int, double> document_to_relevance(8);
        
        for_each(
            policy,
            query.plus_words.begin(),
            query.plus_words.end(),
            [this, &document_predicate, &document_to_relevance](std::string_view word) {
                if (!word_to_document_freqs_.count(word) == 0) {
                    const double inverse_document_freq = ComputeWordInverseDocumentFreq(word);
                    for (const auto [document_id, term_freq] : word_to_document_freqs_.at(word)) {
                        const auto& document_data = documents_.at(document_id);
                        if (document_predicate(document_id, document_data.status, document_data.rating)) {
                            document_to_relevance[document_id].ref_to_value += term_freq * inverse_document_freq;
                        }
                    }
                }
            }
        );

    for_each(
            policy,
            query.minus_words.begin(),
            query.minus_words.end(),
            [this, &document_predicate, &document_to_relevance](std::string_view word) {
                if (!word_to_document_freqs_.count(word) == 0) {
                    for (const auto [document_id, _] : word_to_document_freqs_.at(word)) {
                        document_to_relevance.Erase(document_id);
                    }
                }
            }
        );
        
        std::map<int, double> m_document_to_relevance(document_to_relevance.BuildOrdinaryMap());
        std::vector<Document> matched_documents;
        matched_documents.reserve(m_document_to_relevance.size());
        
        for_each(
            policy,
            m_document_to_relevance.begin(), m_document_to_relevance.end(),
            [this, &matched_documents](const auto& document) {
                matched_documents.push_back({
                    document.first,
                    document.second,
                    documents_.at(document.first).rating
                });
            }
        );
        return matched_documents;
}

    


