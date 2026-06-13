#ifndef __AM_LEXER_H__
#define __AM_LEXER_H__

#include <stdint.h>
#include <wchar.h>
#include <wctype.h>
#include <string.h>

/* ===== Token类型定义 ===== */
#define TOKEN_TYPE_SEP        (0)   // 分隔符: 空白字符
#define TOKEN_TYPE_LB         (1)   // 左括号: ( [ {
#define TOKEN_TYPE_RB         (2)   // 右括号: ) ] }
#define TOKEN_TYPE_KEYWORD    (3)   // 关键字: define if cond while lambda begin 等
#define TOKEN_TYPE_BOOLEAN    (4)   // 字面值：#t #f
#define TOKEN_TYPE_UNDEFINED  (5)   // 字面值：#undefined
#define TOKEN_TYPE_NULL       (6)   // 字面值：#null
#define TOKEN_TYPE_NUMBER     (7)   // 字面值：数字，如 -3.14 +12.3 2e-5 等
#define TOKEN_TYPE_SYMBOL     (8)   // 字面值：符号，即单撇号开头的符号，如 'symbol 等
#define TOKEN_TYPE_VARIABLE   (9)   // 变量标识符
#define TOKEN_TYPE_STRING     (10)  // 字符串: "hello"
#define TOKEN_TYPE_QUOTE      (11)  // 各类用于标记quote、quasiquote、unquote的符号，如 , `
#define TOKEN_TYPE_UNEXPECTED (99)  // 意料之外的token

typedef struct am_token_t {
    size_t  index;   // token首字符在code中的偏移
    size_t  length;  // token长度(字符数)
    int32_t type;    // token类型
    int32_t line;    // 行号(从1开始)
    int32_t column;  // 列号(从0开始)
} am_token_t;

// 保留字表（示例）
static const wchar_t* KEYWORDS[] = {
    L"define", L"if", L"cond", L"lambda", L"begin", L"native", L"import", L"...",
    L"quote", L"quasiquote", L"unquote", L"set!", NULL
};


/* ===== 辅助函数 ===== */

// 定界符判断
static inline int is_delimiter(wchar_t c) {
    return c == L'(' || c == L')' || c == L'[' || c == L']' ||
           c == L'{' || c == L'}' || c == L',' || c == L'`' || c == L'"';
}

static inline int is_whitespace(wchar_t c) {
    return c == L' ' || c == L'\t' || c == L'\n' || c == L'\r';
}

static inline int is_escaped(wchar_t *code, int32_t pos) {
    if(pos <= 0) return 0;
    int32_t count = 0, i = pos - 1;
    while(i >= 0 && code[i--] == L'\\') count++;
    return (count & 1);
}

static int32_t parse_string(wchar_t *code, int32_t start, int32_t *end_pos) {
    if(code[start] != L'"') return -1;
    int32_t pos = start + 1;
    while(code[pos]) {
        if(code[pos] == L'"' && !is_escaped(code, pos)) {
            *end_pos = pos + 1;
            return pos - start + 1;
        }
        if(code[pos] == L'\n' || code[pos] == L'\r') return -1;
        pos++;
    }
    return -1;
}

// 🔹 增强：数字字符判断（支持科学计数法）
static inline int is_num_char(wchar_t c) {
    return iswdigit(c) || c == L'.' || c == L'e' || c == L'E' ||
           c == L'+' || c == L'-';
}

// 🔹 增强：更严格的数字字面值判断
static int is_number(wchar_t *code, int32_t start, int32_t len) {
    if(len == 0) return 0;
    wchar_t first = code[start];
    // 首字符必须是 +/- 或数字
    if(!(first == L'-' || first == L'+' || iswdigit(first))) return 0;
    
    int32_t has_digit = 0, has_dot = 0, has_exp = 0;
    for(int32_t i = 0; i < len; i++) {
        wchar_t c = code[start + i];
        if(iswdigit(c)) { has_digit = 1; }
        else if(c == L'.') {
            if(has_dot || has_exp) return 0; // 多个. 或 . 在指数后
            has_dot = 1;
        }
        else if(c == L'e' || c == L'E') {
            if(has_exp || !has_digit) return 0; // 多个e 或 e前无数字
            has_exp = 1;
            // e后可跟 +/-
            if(i+1 < len && (code[start+i+1] == L'+' || code[start+i+1] == L'-')) i++;
            has_digit = 0; // 指数部分需重新验证有数字
        }
        else if(c == L'+' || c == L'-') {
            // +/- 只能出现在开头或e/E之后
            if(i != 0 && code[start+i-1] != L'e' && code[start+i-1] != L'E') return 0;
        }
        else {
            return 0; // 非法字符
        }
    }
    return has_digit; // 必须至少有一个数字
}

// 判断是否为关键字
static int is_keyword(wchar_t *code, int32_t start, int32_t len) {
    if(len == 0) return 0;
    for(int i = 0; KEYWORDS[i]; i++) {
        if(wcsncmp(&code[start], KEYWORDS[i], len) == 0 && KEYWORDS[i][len] == L'\0') {
            return 1;
        }
    }
    return 0;
}


// 🔹 新增：解析 # 开头的特殊字面值
static int32_t parse_hash_literal(wchar_t *code, int32_t start, int32_t *len_out) {
    if(code[start] != L'#') return 0;
    int32_t pos = start + 1;
    
    // #t / #f (布尔值)
    if(code[pos] == L't' || code[pos] == L'f') {
        wchar_t next = code[pos + 1];
        if(!is_delimiter(next) && !is_whitespace(next) && next != L'\0') {
            return 0; // #ta 不是合法布尔值
        }
        *len_out = 2;
        return TOKEN_TYPE_BOOLEAN;
    }
    
    // #undefined
    if(wcsncmp(&code[pos], L"undefined", 9) == 0) {
        wchar_t next = code[pos + 9];
        if(!is_delimiter(next) && !is_whitespace(next) && next != L'\0') return 0;
        *len_out = 10; // # + undefined(9)
        return TOKEN_TYPE_UNDEFINED;
    }
    
    // #null
    if(wcsncmp(&code[pos], L"null", 4) == 0) {
        wchar_t next = code[pos + 4];
        if(!is_delimiter(next) && !is_whitespace(next) && next != L'\0') return 0;
        *len_out = 5; // # + null(4)
        return TOKEN_TYPE_NULL;
    }
    
    return 0; // 不识别的 # 字面值
}

static void update_pos(wchar_t c, int32_t *line, int32_t *column) {
    if(c == L'\n') { *line += 1; *column = 0; }
    else if(c == L'\r') {
        if(*column >= 0) { *line += 1; *column = 0; }
    }
    else if(c != L'\0') { (*column)++; }
}

/* ===== 主Lexer函数 ===== */
int32_t am_lexer(wchar_t *code, am_token_t *tokens) {
    if(!code || !tokens) return -1;
    
    int32_t pos = 0, tok_cnt = 0;
    int32_t line = 1, col = 0;
    int32_t buf_start = -1, buf_line = -1, buf_col = -1;
    
#define EMIT(t_type, t_len, t_idx)          \
    do {                                    \
        am_token_t *t = &tokens[tok_cnt++]; \
        t->index = (t_idx);                 \
        t->length = (t_len);                \
        t->type = (t_type);                 \
        t->line = buf_line;                 \
        t->column = buf_col;                \
        buf_start = -1;                     \
    } while(0)

// 🔹 重写：类型判断逻辑匹配新Token定义
#define FLUSH()                                                 \
    do {                                                        \
        if(buf_start != -1) {                                   \
            int32_t len = pos - buf_start;                      \
            int32_t t_type = TOKEN_TYPE_VARIABLE;               \
            wchar_t first = code[buf_start];                    \
            /* #开头的特殊字面值 */                             \
            if(first == L'#') {                                 \
                int32_t hash_len;                               \
                int32_t hash_type = parse_hash_literal(code, buf_start, &hash_len); \
                if(hash_type != 0 && hash_len == len) {         \
                    t_type = hash_type;                         \
                } else {                                        \
                    t_type = TOKEN_TYPE_UNEXPECTED;             \
                }                                               \
            }                                                   \
            else if (first == L'\'') {                          \
                if (len > 1) t_type = TOKEN_TYPE_SYMBOL;        \
                else t_type = TOKEN_TYPE_QUOTE;                 \
            }                                                   \
            /* 数字字面值 */                                     \
            else if(is_number(code, buf_start, len)) {          \
                t_type = TOKEN_TYPE_NUMBER;                     \
            }                                                   \
            /* 关键字 */                                        \
            else if (is_keyword(code, buf_start, len)) {        \
                t_type = TOKEN_TYPE_KEYWORD;                    \
            }                                                   \
            /* 其他: 变量 */                                     \
            else {                                              \
                t_type = TOKEN_TYPE_VARIABLE;                   \
            }                                                   \
            EMIT(t_type, len, buf_start);                       \
        }                                                       \
    } while(0)

    while(code[pos]) {
        wchar_t c = code[pos];
        
        /* 1. 注释: ; 到行尾 */
        if(c == L';' && !is_escaped(code, pos)) {
            FLUSH();
            while(code[pos] && code[pos] != L'\n' && code[pos] != L'\r')
                update_pos(code[pos++], &line, &col);
            if(code[pos]) update_pos(code[pos++], &line, &col);
            continue;
        }
        
        /* 2. 定界符处理 */
        if(is_delimiter(c) && !is_escaped(code, pos)) {
            FLUSH();
            
            // 2.1 字符串字面量
            if(c == L'"') {
                int32_t end_pos;
                int32_t len = parse_string(code, pos, &end_pos);
                if(len < 0) return -1;
                buf_line = line; buf_col = col;
                EMIT(TOKEN_TYPE_STRING, len, pos);
                while(pos < end_pos) update_pos(code[pos++], &line, &col);
                continue;
            }
            
            // 2.2 { 特殊转换: { -> ( + begin (虚拟token)
            if(c == L'{') {
                buf_line = line; buf_col = col;
                EMIT(TOKEN_TYPE_LB, 1, pos);
                
                am_token_t *t2 = &tokens[tok_cnt++];
                t2->index = -1; t2->length = 5; t2->type = TOKEN_TYPE_KEYWORD;
                t2->line = line; t2->column = col + 1;
                
                update_pos(c, &line, &col);
                pos++;
                continue;
            }
            
            // 🔹 2.4 普通定界符类型映射
            int32_t t_type;
            if(c == L'(' || c == L'[') 
                t_type = TOKEN_TYPE_LB;
            else if(c == L')' || c == L']' || c == L'}') 
                t_type = TOKEN_TYPE_RB;
            // 🔹 逗号、反引号 → QUOTE 类型
            else if(c == L',' || c == L'`') 
                t_type = TOKEN_TYPE_QUOTE;
            else 
                t_type = TOKEN_TYPE_SEP;
            
            buf_line = line; buf_col = col;
            EMIT(t_type, 1, pos);
            update_pos(c, &line, &col);
            pos++;
            continue;
        }
        
        /* 3. 空白字符 */
        if(is_whitespace(c)) {
            FLUSH();
            update_pos(c, &line, &col);
            if(c == L'\r' && code[pos+1] == L'\n')
                update_pos(code[++pos], &line, &col);
            pos++;
            continue;
        }
        
        /* 4. 普通字符累积 */
        if(buf_start == -1) {
            buf_start = pos;
            buf_line = line;
            buf_col = col;
        }
        update_pos(c, &line, &col);
        pos++;
    }
    
    FLUSH();
    return tok_cnt;
}

// 安全获取 token 文本（处理虚拟 token）
const wchar_t* token_text(am_token_t *tok, wchar_t *code) {
    static wchar_t buf[256];
    if(tok->index == -1) {
        // 虚拟 token
        if(tok->type == TOKEN_TYPE_KEYWORD && tok->length == 5) return L"begin";
        return L"[virtual]";
    }
    // 真实 token
    int32_t len = tok->length < 255 ? tok->length : 255;
    wcsncpy(buf, &code[tok->index], len);
    buf[len] = L'\0';
    return buf;
}

#endif
