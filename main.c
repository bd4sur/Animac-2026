#include <stdio.h>
#include <stdlib.h>
#include <locale.h>
#include <wchar.h>
#include <wctype.h>
#include <string.h>

#include "utils.h"
#include "lexer.h"
#include "highlight.h"

// 打印 token 类型名称
const wchar_t* type_name(int32_t type) {
    switch(type) {
        case TOKEN_TYPE_SEP: return L"SEP";
        case TOKEN_TYPE_LB: return L"LB";
        case TOKEN_TYPE_RB: return L"RB";
        case TOKEN_TYPE_KEYWORD: return L"KEYWORD";
        case TOKEN_TYPE_BOOLEAN: return L"BOOLEAN";
        case TOKEN_TYPE_UNDEFINED: return L"UNDEFINED";
        case TOKEN_TYPE_NULL: return L"NULL";
        case TOKEN_TYPE_NUMBER: return L"NUMBER";
        case TOKEN_TYPE_SYMBOL: return L"SYMBOL";
        case TOKEN_TYPE_VARIABLE: return L"VARIABLE";
        case TOKEN_TYPE_STRING: return L"STRING";
        case TOKEN_TYPE_QUOTE: return L"QUOTE";
        default: return L"UNEXPECTED";
    }
}

int main(int argc, char* argv[]) {
    if(!setlocale(LC_ALL, "")) return -1;

    // 读取源码文件（支持UTF-8）
    if(argc < 2) {
        printf("Usage: %s <source.scm>\n", argv[0]);
        return 1;
    }

    wchar_t *code = read_file_to_wchar(argv[1]);

    int32_t code_length = wcslen(code);

    am_token_t *tokens = (am_token_t *)calloc(code_length, sizeof(am_token_t));
    
    int32_t count = am_lexer(code, tokens);
    if(count < 0) {
        printf("词法分析错误!\n");
        return 1;
    }

    for (int32_t i = 0; i < count; i++) {
        printf("[%4d] %12ls @%5d+%3d  %ls\n", 
            i, type_name(tokens[i].type), 
            tokens[i].index, tokens[i].length,
            token_text(&tokens[i], code));
    }

    printf("\033[1m=== 语法高亮输出 ===\033[0m\n");
    am_print_highlighted(code, tokens, count);

    free(tokens);
    return 0;
}
