#include "document.h"
#include "remove_duplicates.h"
#include "request_queue.h"
#include "log_duration.h"
#include "paginator.h"
#include "search_server.h"
#include "string_processing.h"

using namespace std;

void AddDocument(SearchServer& se, int i, string s, DocumentStatus d, vector<int> r) {
    se.AddDocument(i,s,d,r);
}



