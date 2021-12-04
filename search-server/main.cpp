#include <algorithm>
#include <cmath>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>
#include <iostream>

using namespace std;

const int MAX_RESULT_DOCUMENT_COUNT = 5;

string ReadLine() {
    string s;
    getline(cin, s);
    return s;
}

int ReadLineWithNumber() {
    int result;
    cin >> result;
    ReadLine();
    return result;
}

vector<string> SplitIntoWords(const string& text) {
    vector<string> words;
    string word;
    for (const char c : text) {
        if (c == ' ') {
            if (!word.empty()) {
                words.push_back(word);
                word.clear();
            }
        } else {
            word += c;
        }
    }
    if (!word.empty()) {
        words.push_back(word);
    }

    return words;
}

struct Document {
    int id;
    double relevance;
    int rating;
};

enum class DocumentStatus {
    ACTUAL,
    IRRELEVANT,
    BANNED,
    REMOVED,
};

class SearchServer {
public:
    void SetStopWords(const string& text) {
        for (const string& word : SplitIntoWords(text)) {
            stop_words_.insert(word);
        }
    }

    void AddDocument(int document_id, const string& document, DocumentStatus status, const vector<int>& ratings) {
        const vector<string> words = SplitIntoWordsNoStop(document);
        const double inv_word_count = 1.0 / words.size();
        for (const string& word : words) {
            word_to_document_freqs_[word][document_id] += inv_word_count;
        }
        documents_.emplace(document_id,
            DocumentData{
                ComputeAverageRating(ratings),
                status
            });
    }

    vector<Document> FindTopDocuments(const string& raw_query, DocumentStatus status_document) const {
        return(FindTopDocuments(raw_query, [status_document](int document_id, DocumentStatus status, int rating) { return status == status_document; }));
    }

    vector<Document> FindTopDocuments(const string& raw_query) const {
        return(FindTopDocuments(raw_query, DocumentStatus::ACTUAL));
    }

    template<typename KeyMapper>
    vector<Document> FindTopDocuments(const string& raw_query, KeyMapper key_mapper) const {
        const Query query = ParseQuery(raw_query);
        auto matched_documents = FindAllDocuments(query, key_mapper);

        sort(matched_documents.begin(), matched_documents.end(),
             [](const Document& lhs, const Document& rhs) {
                if (abs(lhs.relevance - rhs.relevance) < 1e-6) {
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

    int GetDocumentCount() const {
        return documents_.size();
    }

    tuple<vector<string>, DocumentStatus> MatchDocument(const string& raw_query, int document_id) const {
        const Query query = ParseQuery(raw_query);
        vector<string> matched_words;
        for (const string& word : query.plus_words) {
            if (word_to_document_freqs_.count(word) == 0) {
                continue;
            }
            if (word_to_document_freqs_.at(word).count(document_id)) {
                matched_words.push_back(word);
            }
        }
        for (const string& word : query.minus_words) {
            if (word_to_document_freqs_.count(word) == 0) {
                continue;
            }
            if (word_to_document_freqs_.at(word).count(document_id)) {
                matched_words.clear();
                break;
            }
        }
        return {matched_words, documents_.at(document_id).status};
    }

private:
    struct DocumentData {
        int rating;
        DocumentStatus status;
    };

    set<string> stop_words_;
    map<string, map<int, double>> word_to_document_freqs_;
    map<int, DocumentData> documents_;

    bool IsStopWord(const string& word) const {
        return stop_words_.count(word) > 0;
    }

    vector<string> SplitIntoWordsNoStop(const string& text) const {
        vector<string> words;
        for (const string& word : SplitIntoWords(text)) {
            if (!IsStopWord(word)) {
                words.push_back(word);
            }
        }
        return words;
    }

    static int ComputeAverageRating(const vector<int>& ratings) {
        if (ratings.empty()) {
            return 0;
        }
        int rating_sum = 0;
        for (const int rating : ratings) {
            rating_sum += rating;
        }
        return rating_sum / static_cast<int>(ratings.size());
    }

    struct QueryWord {
        string data;
        bool is_minus;
        bool is_stop;
    };

    QueryWord ParseQueryWord(string text) const {
        bool is_minus = false;
        // Word shouldn't be empty
        if (text[0] == '-') {
            is_minus = true;
            text = text.substr(1);
        }
        return {
            text,
            is_minus,
            IsStopWord(text)
        };
    }

    struct Query {
        set<string> plus_words;
        set<string> minus_words;
    };

    Query ParseQuery(const string& text) const {
        Query query;
        for (const string& word : SplitIntoWords(text)) {
            const QueryWord query_word = ParseQueryWord(word);
            if (!query_word.is_stop) {
                if (query_word.is_minus) {
                    query.minus_words.insert(query_word.data);
                } else {
                    query.plus_words.insert(query_word.data);
                }
            }
        }
        return query;
    }

    // Existence required
    double ComputeWordInverseDocumentFreq(const string& word) const {
        return log(GetDocumentCount() * 1.0 / word_to_document_freqs_.at(word).size());
    }

    template<typename KeyMapper>
    vector<Document> FindAllDocuments(const Query& query, KeyMapper key_mapper) const {
        map<int, double> document_to_relevance;
        for (const string& word : query.plus_words) {
            if (word_to_document_freqs_.count(word) == 0) {
                continue;
            }
            const double inverse_document_freq = ComputeWordInverseDocumentFreq(word);
            for (const auto [document_id, term_freq] : word_to_document_freqs_.at(word)) {
                auto document_information = documents_.at(document_id);
                if (key_mapper(document_id, document_information.status, document_information.rating)){
                    document_to_relevance[document_id] += term_freq * inverse_document_freq;
                }
            }
        }

        for (const string& word : query.minus_words) {
            if (word_to_document_freqs_.count(word) == 0) {
                continue;
            }
            for (const auto [document_id, _] : word_to_document_freqs_.at(word)) {
                document_to_relevance.erase(document_id);
            }
        }

        vector<Document> matched_documents;
        for (const auto [document_id, relevance] : document_to_relevance) {
            matched_documents.push_back({
                document_id,
                relevance,
                documents_.at(document_id).rating
                });

        }
        return matched_documents;
    }
};

void PrintDocument(const Document& document) {
    cout << "{ "s
         << "document_id = "s << document.id << ", "s
         << "relevance = "s << document.relevance << ", "s
         << "rating = "s << document.rating
         << " }"s << endl;
}

//-----------------Универсальный вывод для контейнеров-----------------

template <typename Term>
ostream& Print(ostream& out, const Term& container){
    for (auto it = container.begin(); it != container.end(); ++it)
        {
            if (*it!=*--container.end()){
                out << *it << ", ";
            }else {out << *it;}
        }
    return out;
}

template <typename Term1, typename Term2>
ostream& operator<<(ostream& out, const pair<Term1,Term2>& container) {

    out << container.first;
    out << ": "s;
    out << container.second;

    return out;
}

template <typename Term1, typename Term2>
ostream& operator<<(ostream& out, const map<Term1,Term2>& container) {
    out << "{"s;
    Print(out,container);
    out << "}"s;
    return out;
}

template <typename Term>
ostream& operator<<(ostream& out, const set<Term>& container) {
    out << "{"s;
    Print(out,container);
    out << "}"s;
    return out;
}

template <typename Term>
ostream& operator<<(ostream& out, const vector<Term>& container) {
    out << "["s;
    Print(out,container);
    out << "]"s;
    return out;
}

//-----------------Конец универсального вывода для контейнеров-----------------

//-----------------Реализация своего ASSERT, ASSERT_EQUAL и ASSERT_HINT, ASSERT_EQUAL_HINT-----------------

void AssertImpl(bool value, const string& expr_str, const string& file, const string& func, unsigned line,
                const string& hint) {
    if (!value) {
        cout << file << "("s << line << "): "s << func << ": "s;
        cout << "ASSERT("s << expr_str << ") failed."s;
        if (!hint.empty()) {
            cout << " Hint: "s << hint;
        }
        cout << endl;
        abort();
    }
}

#define ASSERT(expr) AssertImpl(!!(expr), #expr, __FILE__, __FUNCTION__, __LINE__, ""s)

#define ASSERT_HINT(expr, hint) AssertImpl(!!(expr), #expr, __FILE__, __FUNCTION__, __LINE__, (hint))

template <typename T, typename U>
void AssertEqualImpl(const T& t, const U& u, const string& t_str, const string& u_str, const string& file,
                     const string& func, unsigned line, const string& hint) {
    if (t != u) {
        cout << boolalpha;
        cout << file << "("s << line << "): "s << func << ": "s;
        cout << "ASSERT_EQUAL("s << t_str << ", "s << u_str << ") failed: "s;
        cout << t << " != "s << u << "."s;
        if (!hint.empty()) {
            cout << " Hint: "s << hint;
        }
        cout << endl;
        abort();
    }
}

#define ASSERT_EQUAL(a, b) AssertEqualImpl((a), (b), #a, #b, __FILE__, __FUNCTION__, __LINE__, ""s)

#define ASSERT_EQUAL_HINT(a, b, hint) AssertEqualImpl((a), (b), #a, #b, __FILE__, __FUNCTION__, __LINE__, (hint))

//-----------------Конец реализация своего ASSERT, ASSERT_EQUAL и ASSERT_HINT, ASSERT_EQUAL_HINT-----------------

// -------- Начало модульных тестов поисковой системы ----------

// Тест на добавление документов и их нахождение по поисковому запросу

    void TestForAddDocument() {
        vector<int> ratings = {1,2,3};
        {
            SearchServer server;
            ASSERT_EQUAL(server.GetDocumentCount(), 0);
            server.AddDocument(10, "cat cat dog"s, DocumentStatus::ACTUAL, ratings);
            server.AddDocument(11, "cat cat dog cat"s, DocumentStatus::ACTUAL, ratings);
            ASSERT_EQUAL(server.GetDocumentCount(), 2);
            server.AddDocument(12, "cat cat dog cat cat"s, DocumentStatus::ACTUAL, ratings);
            server.AddDocument(13, "cat cat dog cat god cat cat cat"s, DocumentStatus::ACTUAL, ratings);
            server.AddDocument(21, "cat cat cat"s, DocumentStatus::ACTUAL, ratings);
            server.AddDocument(96, "dog"s, DocumentStatus::ACTUAL, ratings);
            ASSERT_EQUAL(server.GetDocumentCount(), 6);
            const auto found_docs = server.FindTopDocuments("dog"s);
            ASSERT_EQUAL(found_docs.size(), 5u);
        }

        {
            SearchServer server;
            ASSERT_EQUAL(server.GetDocumentCount(), 0);
            server.AddDocument(10, "cat cat dog"s, DocumentStatus::ACTUAL, ratings);
            server.AddDocument(11, "cat"s, DocumentStatus::ACTUAL, ratings);
            server.AddDocument(12, "cat cat"s, DocumentStatus::ACTUAL, ratings);
            ASSERT_EQUAL(server.GetDocumentCount(), 3);
            server.AddDocument(13, "dog"s, DocumentStatus::ACTUAL, ratings);
            ASSERT_EQUAL(server.GetDocumentCount(), 4);
            const auto found_docs = server.FindTopDocuments("dog"s);
            ASSERT_EQUAL(found_docs.size(), 2u);
        }
    }

// Тест проверяет, что поисковая система исключает стоп-слова при добавлении документов
    void TestExcludeStopWordsFromAddedDocumentContent() {
        const int doc_id = 42;
        const string content = "cat in the city"s;
        const vector<int> ratings = {1, 2, 3};
        // Сначала убеждаемся, что поиск слова, не входящего в список стоп-слов,
        // находит нужный документ
        {
            SearchServer server;
            server.AddDocument(doc_id, content, DocumentStatus::ACTUAL, ratings);
            const auto found_docs = server.FindTopDocuments("in"s);
            ASSERT_EQUAL(found_docs.size(), 1u);
            const Document& doc0 = found_docs[0];
            ASSERT_EQUAL(doc0.id, doc_id);
        }

        // Затем убеждаемся, что поиск этого же слова, входящего в список стоп-слов,
        // возвращает пустой результат
        {
            SearchServer server;
            server.SetStopWords("in the"s);
            server.AddDocument(doc_id, content, DocumentStatus::ACTUAL, ratings);
            ASSERT(server.FindTopDocuments("in"s).empty());
        }
    }

// Разместите код остальных тестов здесь

// Тест, проверяющий, исключаются ли документы в которых было найдено минус слово

    void TestExcludeMinusWordsFromResultOfAddedDocument() {
        const int doc_id = 42;
        const string content = "cat in the city"s;
        const vector<int> ratings = {1, 2, 3};
        {
            SearchServer server;
            server.AddDocument(doc_id, content, DocumentStatus::ACTUAL, ratings);
            const auto found_docs = server.FindTopDocuments("hello im test -in"s);
            ASSERT(found_docs.empty());
        }

        {
            SearchServer server;
            server.AddDocument(doc_id, content, DocumentStatus::ACTUAL, ratings);
            const auto found_docs = server.FindTopDocuments("hello im -test in"s);
            ASSERT_EQUAL(found_docs.size(), 1u);
        }
    }

// Тест на корректность ввода слов во внутреннее хранилище класса

    void TestForMatchingAllWordsFromDocument() {
        const int doc_id = 42;
        const string content = "just random words put in here to check test hello hi im friedly"s;
        const vector<int> ratings = {1, 2, 3};
        {
            vector<string> words;
            DocumentStatus status;
            SearchServer server;
            string query = "hello hi wanna see if that works as i wish"s;
            server.AddDocument(doc_id, content, DocumentStatus::ACTUAL, ratings);
            tie(words, status) = server.MatchDocument(query, 42);
            vector<string> check = {"hello"s, "hi"s};
            ASSERT_EQUAL(words, check);

        }

        {
            vector<string> words;
            DocumentStatus status;
            SearchServer server;
            string query = "random wanna see if that shit works as i wish to"s;
            server.AddDocument(doc_id, content, DocumentStatus::ACTUAL, ratings);
            tie(words, status) = server.MatchDocument(query, 42);
            vector<string> check = {"random"s, "to"s};
            ASSERT_EQUAL(words, check);
        }

        {
            vector<string> words;
            DocumentStatus status;
            SearchServer server;
            string query = "random -hello see if that shit works as i wish to"s;
            server.AddDocument(doc_id, content, DocumentStatus::ACTUAL, ratings);
            tie(words, status) = server.MatchDocument(query, doc_id);
            vector<string> check  = {};
            ASSERT_EQUAL(words, check);
        }
    }

// Тест на проверку сортировки найденных документов по релевантности

    void TestForRelevanceSorting() {
        const vector<int> ratings = {1, 2, 3};
        {
            SearchServer server;
            server.AddDocument(10, "cat cat dog"s, DocumentStatus::ACTUAL, ratings);
            server.AddDocument(11, "cat cat dog cat"s, DocumentStatus::ACTUAL, ratings);
            server.AddDocument(12, "cat cat dog cat cat"s, DocumentStatus::ACTUAL, ratings);
            server.AddDocument(13, "cat cat dog cat god cat cat cat"s, DocumentStatus::ACTUAL, ratings);
            server.AddDocument(21, "cat cat cat"s, DocumentStatus::ACTUAL, ratings);
            server.AddDocument(96, "dog"s, DocumentStatus::ACTUAL, ratings);
            auto found_documents = server.FindTopDocuments("cat"s);
            for (int i = 0; i < static_cast<int>(found_documents.size()) - 1; ++i){
                ASSERT(found_documents[i].relevance >= found_documents[i+1].relevance);
            }
        }
    }

// Тест на правильность подсчёта среднего рейтинга

    void TestForCountingAverageRating() {
        {
            SearchServer server;
            server.AddDocument(10, "cat cat dog"s, DocumentStatus::ACTUAL, {10, 20, 30});
            server.AddDocument(21, "cat cat cat"s, DocumentStatus::ACTUAL, {1, 2, 3});
            server.AddDocument(96, "dog"s, DocumentStatus::ACTUAL, {1, 2, 3});
            const auto found_documents = server.FindTopDocuments("cat");
            ASSERT_EQUAL(found_documents[0].rating, (1 + 2 + 3) / 3);
            ASSERT_EQUAL(found_documents[1].rating, (10 + 20 + 30) / 3);
        }
    }

// Тест на фильтрацию по заданному предикату

    void TestForPredicateFilter() {
        vector<int> ratings = {1, 2, 3};
        SearchServer server;
        server.AddDocument(10, "cat cat dog"s, DocumentStatus::ACTUAL, ratings);
        server.AddDocument(11, "cat cat dog cat"s, DocumentStatus::ACTUAL, ratings);
        server.AddDocument(12, "cat cat dog cat cat"s, DocumentStatus::ACTUAL, ratings);
        server.AddDocument(13, "cat cat dog cat god cat cat cat"s, DocumentStatus::ACTUAL, ratings);
        server.AddDocument(21, "cat cat cat"s, DocumentStatus::ACTUAL, ratings);
        server.AddDocument(96, "dog"s, DocumentStatus::ACTUAL, ratings);
        {
            auto found_documents = server.FindTopDocuments("cat"s, [](int document_id, DocumentStatus status, int rating) { return rating > 2; });
            ASSERT(found_documents.empty());
        }

        {
            auto found_documents = server.FindTopDocuments("cat"s, [](int document_id, DocumentStatus status, int rating) { return rating > 1; });
            ASSERT_EQUAL(found_documents.size(), 5u);
        }

        {
            auto found_documents = server.FindTopDocuments("cat"s, [](int document_id, DocumentStatus status, int rating) { return document_id == 21; });
            ASSERT_EQUAL(found_documents.size(), 1u);
            ASSERT_EQUAL(found_documents[0].id, 21);
        }

        {
            auto found_documents = server.FindTopDocuments("cat"s, [](int document_id, DocumentStatus status, int rating) { return status == DocumentStatus::REMOVED; });
            ASSERT(found_documents.empty());
            server.AddDocument(98, "cat"s, DocumentStatus::REMOVED, ratings);
            found_documents = server.FindTopDocuments("cat"s, [](int document_id, DocumentStatus status, int rating) { return status == DocumentStatus::REMOVED; });
            ASSERT_EQUAL(found_documents.size(), 1u);
            ASSERT_EQUAL(found_documents[0].id, 98);
        }
    }

// Тест на поиск по заданному статусу

    void TestForStatusSearch() {
        vector<int> ratings = {1, 3, 5};
        SearchServer server;
        server.AddDocument(10, "cat cat dog"s, DocumentStatus::ACTUAL, ratings);
        server.AddDocument(11, "cat"s, DocumentStatus::ACTUAL, ratings);
        server.AddDocument(12, "cat cat"s, DocumentStatus::IRRELEVANT, ratings);
        server.AddDocument(13, "dog"s, DocumentStatus::BANNED, ratings);
        {
            const auto found_docs = server.FindTopDocuments("dog"s, DocumentStatus::BANNED);
            ASSERT_EQUAL(found_docs.size(), 1u);
            ASSERT_EQUAL(found_docs[0].id, 13);
        }

        {
            const auto found_docs = server.FindTopDocuments("cat"s, DocumentStatus::IRRELEVANT);
            ASSERT_EQUAL(found_docs.size(), 1u);
            ASSERT_EQUAL(found_docs[0].id, 12);
        }

        {
            const auto found_docs = server.FindTopDocuments("cat dog"s, DocumentStatus::REMOVED);
            ASSERT(found_docs.empty());
        }

        {
            const auto found_docs = server.FindTopDocuments("cat dog"s, DocumentStatus::ACTUAL);
            ASSERT_EQUAL(found_docs.size(), 2u);
            ASSERT_EQUAL(found_docs[0].id, 10);
            ASSERT_EQUAL(found_docs[1].id, 11);
        }
    }

// Тест на корректность вычисления релевантности

    void TestForCorrectRelevanceCalculations(){
        vector<int> ratings = {1, 2, 3};
        {
            SearchServer server;
            server.AddDocument(10, "cat cat dog"s, DocumentStatus::ACTUAL, ratings);
            server.AddDocument(11, "cat cat dog cat"s, DocumentStatus::ACTUAL, ratings);
            server.AddDocument(12, "cat cat dog cat cat"s, DocumentStatus::ACTUAL, ratings);
            server.AddDocument(13, "cat cat dog cat god cat cat cat"s, DocumentStatus::ACTUAL, ratings);
            server.AddDocument(21, "cat cat cat"s, DocumentStatus::ACTUAL, ratings);
            server.AddDocument(96, "dog"s, DocumentStatus::ACTUAL, ratings);
            auto found_documents = server.FindTopDocuments("cat"s);
            /*
            Ручной подсчёт релевантностей, которые должны получится
            Подсчёт глобального IDF: IDF = log (6. / 5) = 0.182322
            Документ 10: Кол-во слов: 3; Кол-во вхождений слов запроса в документ: 2; TF = 2. / 3 = 0.666667; TF-IDF = 0.666667 * 0.182322 = 0.121548
            Документ 11: Кол-во слов: 4; Кол-во вхождений слов запроса в документ: 3; TF = 3. / 4 = 0.75    ; TF-IDF = 0.75 * 0.182322     = 0.136741
            Документ 12: Кол-во слов: 5; Кол-во вхождений слов запроса в документ: 4; TF = 4. / 5 = 0.8     ; TF-IDF = 0.8 * 0.182322      = 0.145857
            Документ 13: Кол-во слов: 8; Кол-во вхождений слов запроса в документ: 6; TF = 6. / 8 = 0.75    ; TF-IDF = 0.75 * 0.182322     = 0.136741
            Документ 21: Кол-во слов: 3; Кол-во вхождений слов запроса в документ: 3; TF = 3. / 3 = 1       ; TF-IDF = 1 * 0.182322        = 0.182322
            */
            ASSERT(found_documents[0].relevance - 0.182322 < 1e-4);
            ASSERT(found_documents[1].relevance - 0.145857 < 1e-4);
            ASSERT(found_documents[2].relevance - 0.136741 < 1e-4);
            ASSERT(found_documents[3].relevance - 0.136741 < 1e-4);
            ASSERT(found_documents[4].relevance - 0.121548 < 1e-4);
        }
    }

// Макрос и функция для него для запуска тестов и вывода в поток cerr сообщения OK, если тест прошёл успешно

template <typename Term>
void RunTestImpl(const Term function, const string& name) {
    function();
    cerr << name << " OK"s << endl;
}

#define RUN_TEST(func)  RunTestImpl( (func), #func )

// Функция TestSearchServer является точкой входа для запуска тестов

void TestSearchServer() {
    RUN_TEST(TestForAddDocument);
    RUN_TEST(TestExcludeStopWordsFromAddedDocumentContent);
    RUN_TEST(TestExcludeMinusWordsFromResultOfAddedDocument);
    RUN_TEST(TestForMatchingAllWordsFromDocument);
    RUN_TEST(TestForRelevanceSorting);
    RUN_TEST(TestForCountingAverageRating);
    RUN_TEST(TestForPredicateFilter);
    RUN_TEST(TestForStatusSearch);
    RUN_TEST(TestForCorrectRelevanceCalculations);
}

// --------- Окончание модульных тестов поисковой системы -----------

int main() {
    TestSearchServer();
    cout << "Search server testing finished"s << endl;
    return(0);
}
