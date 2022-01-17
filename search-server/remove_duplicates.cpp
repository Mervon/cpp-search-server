#include "remove_duplicates.h"

#include <map>
#include <string>
#include <vector>

bool MyCompare (const std::map<std::string, double>& lhs, const std::map<std::string, double>& rhs) {
    if (lhs.size() != rhs.size()) {
        return (false);
    }

    for (auto it = lhs.begin(); it != lhs.end(); ++it) {
        if (rhs.find(it -> first) == rhs.end()) {
            return (false);
        }
    }

    return (true);
}

void RemoveDuplicates(SearchServer& search_server) {
    using namespace std::string_literals;

    std::map<int, std::map<std::string, double>> temp;

    std::set<int> marks;

    std::vector<int> ids;

    for (int id : search_server) {
        const auto content = search_server.GetWordFrequencies(id);
        for (auto& cont : content) {
            temp[id][cont.first] = cont.second;
            ids.push_back(id);
        }
    }

    int i = 0;
    for (int id = ids[i]; i < ids.size(); id = ids[++i]) {
        int j = i;
        for (int id_ = ids[j]; j < ids.size(); id_ = ids[++j]) {
            if (id != id_ && MyCompare(temp[id], temp[id_]) ) {
                marks.insert(id_);
            }
        }
    }

    for (int i : marks) {
        std::cout << "Found duplicate document id " << i << std::endl;
        search_server.RemoveDocument(i);
    }
}









