
#include "string_processing.h"

#include <string_view>

std::vector<std::string> SplitIntoWords(const std::string& text) {
    std::vector<std::string> words;

    std::string word;

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

std::vector<std::string_view> SplitIntoWords(std::string_view str) {
    std::vector<std::string_view> result;
    const int64_t pos_end = str.npos;
    
    while (true) {
        
        int64_t space = str.find(' ', 0);
        
        if (space == pos_end) {
            result.push_back(str.substr(0));
            break;
        }else{
            auto tmp = str.substr(0, space);
            result.push_back(tmp);
        }
        str.remove_prefix(space + 1);
    }

    return result;
}