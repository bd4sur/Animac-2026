/* JS -> Scheme (non-standard) mechanical translator.
 * Single-file, self-contained C99 program.
 * Build: gcc -std=c99 -Wall -Wextra -o jstoscm jstoscm.c
 * Usage: ./jstoscm < file.js   or   ./jstoscm file.js
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdarg.h>
#include <wchar.h>

/* ======================== String builder ======================== */

typedef struct {
    char *buf;
    size_t len;
    size_t cap;
} SB;

static void sb_init(SB *sb) {
    sb->buf = NULL;
    sb->len = 0;
    sb->cap = 0;
}

static void sb_free(SB *sb) {
    free(sb->buf);
    sb->buf = NULL;
    sb->len = 0;
    sb->cap = 0;
}

static void sb_grow(SB *sb, size_t need) {
    if (sb->len + need + 1 <= sb->cap) return;
    size_t newcap = sb->cap ? sb->cap * 2 : 256;
    while (newcap < sb->len + need + 1) newcap *= 2;
    sb->buf = (char *)realloc(sb->buf, newcap);
    sb->cap = newcap;
}

static void sb_append(SB *sb, const char *s) {
    if (!s) return;
    size_t n = strlen(s);
    sb_grow(sb, n);
    memcpy(sb->buf + sb->len, s, n);
    sb->len += n;
    sb->buf[sb->len] = '\0';
}

static void sb_appendf(SB *sb, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(NULL, 0, fmt, ap);
    va_end(ap);
    if (n < 0) return;
    sb_grow(sb, (size_t)n);
    va_start(ap, fmt);
    vsnprintf(sb->buf + sb->len, (size_t)n + 1, fmt, ap);
    va_end(ap);
    sb->len += (size_t)n;
}

static char *xstrdup(const char *s) {
    if (!s) return NULL;
    size_t n = strlen(s) + 1;
    char *p = (char *)malloc(n);
    if (p) memcpy(p, s, n);
    return p;
}

/* ======================== Lexer ======================== */

typedef enum {
    T_EOF,
    T_NUM, T_STR, T_ID,
    T_IF, T_ELSE, T_WHILE, T_VAR, T_FUNCTION, T_RETURN,
    T_CONTINUE, T_BREAK,
    T_TRUE, T_FALSE, T_NULL, T_UNDEFINED,
    T_LPAREN, T_RPAREN, T_LBRACKET, T_RBRACKET, T_LBRACE, T_RBRACE,
    T_SEMI, T_COMMA, T_COLON, T_QUESTION,
    T_NOT, T_PLUS, T_MINUS, T_MUL, T_DIV, T_MOD, T_ASSIGN, T_POW,
    T_LT, T_GT, T_EQ, T_LE, T_GE, T_OR, T_AND, T_INC, T_DEC, T_ARROW
} TokType;

typedef struct {
    TokType type;
    char *value;
    int line;
    int col;
} Token;

typedef struct {
    Token *data;
    int n;
    int cap;
} TokList;

static void tl_init(TokList *tl) {
    tl->data = NULL;
    tl->n = 0;
    tl->cap = 0;
}

static void tl_add(TokList *tl, TokType type, const char *value, int line, int col) {
    if (tl->n + 1 > tl->cap) {
        tl->cap = tl->cap ? tl->cap * 2 : 64;
        tl->data = (Token *)realloc(tl->data, tl->cap * sizeof(Token));
    }
    Token *t = &tl->data[tl->n++];
    t->type = type;
    t->value = value ? xstrdup(value) : NULL;
    t->line = line;
    t->col = col;
}

static void lex_error(int line, int col, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    fprintf(stderr, "词法错误 @ %d:%d: ", line, col);
    vfprintf(stderr, fmt, ap);
    fprintf(stderr, "\n");
    va_end(ap);
    exit(1);
}

static Token *tokenize(const char *src, int *out_count) {
    TokList tl;
    tl_init(&tl);
    int i = 0;
    int len = (int)strlen(src);
    int line = 1, col = 1;
    int parenDepth = 0, bracketDepth = 0, braceDepth = 0;

#define PEEK(off) ((i + (off) < len) ? src[i + (off)] : '\0')

    while (i < len) {
        char ch = PEEK(0);

        if (ch == ' ' || ch == '\t' || ch == '\r') {
            i++; col++;
            continue;
        }

        if (ch == '\n') {
            i++; line++; col = 1;
            if (parenDepth == 0 && bracketDepth == 0) {
                if (tl.n == 0 || tl.data[tl.n - 1].type != T_SEMI)
                    tl_add(&tl, T_SEMI, NULL, line, col);
            }
            continue;
        }

        /* comments */
        if (ch == '/' && PEEK(1) == '/') {
            i += 2; col += 2;
            while (i < len && PEEK(0) != '\n') { i++; col++; }
            continue;
        }
        if (ch == '/' && PEEK(1) == '*') {
            i += 2; col += 2;
            while (i < len) {
                if (PEEK(0) == '*' && PEEK(1) == '/') {
                    i += 2; col += 2;
                    break;
                }
                if (PEEK(0) == '\n') { i++; line++; col = 1; }
                else { i++; col++; }
            }
            continue;
        }

        /* number */
        if (isdigit((unsigned char)ch) || (ch == '.' && isdigit((unsigned char)PEEK(1)))) {
            int startLine = line, startCol = col;
            SB num; sb_init(&num);
            int hasDigits = 0;
            while (i < len && isdigit((unsigned char)PEEK(0))) {
                sb_appendf(&num, "%c", PEEK(0));
                i++; col++; hasDigits = 1;
            }
            if (PEEK(0) == '.') {
                sb_appendf(&num, "%c", PEEK(0));
                i++; col++;
                while (i < len && isdigit((unsigned char)PEEK(0))) {
                    sb_appendf(&num, "%c", PEEK(0));
                    i++; col++; hasDigits = 1;
                }
            }
            if (PEEK(0) == 'e' || PEEK(0) == 'E') {
                char next = PEEK(1);
                if (isdigit((unsigned char)next) ||
                    ((next == '+' || next == '-') && isdigit((unsigned char)PEEK(2)))) {
                    sb_appendf(&num, "%c", PEEK(0));
                    i++; col++;
                    if (next == '+' || next == '-') {
                        sb_appendf(&num, "%c", PEEK(0));
                        i++; col++;
                    }
                    while (i < len && isdigit((unsigned char)PEEK(0))) {
                        sb_appendf(&num, "%c", PEEK(0));
                        i++; col++; hasDigits = 1;
                    }
                }
            }
            if (!hasDigits) lex_error(line, col, "非法数字 \"%s\"", num.buf);
            tl_add(&tl, T_NUM, num.buf, startLine, startCol);
            sb_free(&num);
            continue;
        }

        /* string */
        if (ch == '"') {
            int startLine = line, startCol = col;
            i++; col++;
            SB str; sb_init(&str);
            while (i < len && PEEK(0) != '"') {
                if (PEEK(0) == '\\') {
                    sb_appendf(&str, "%c", PEEK(0));
                    i++; col++;
                    if (i < len) {
                        sb_appendf(&str, "%c", PEEK(0));
                        if (PEEK(0) == '\n') { line++; col = 1; }
                        else { col++; }
                        i++;
                    }
                    continue;
                }
                sb_appendf(&str, "%c", PEEK(0));
                if (PEEK(0) == '\n') { line++; col = 1; }
                else { col++; }
                i++;
            }
            if (PEEK(0) != '"') lex_error(startLine, startCol, "未终止的字符串");
            i++; col++;
            tl_add(&tl, T_STR, str.buf, startLine, startCol);
            sb_free(&str);
            continue;
        }

        /* identifier / keyword */
        if (isalpha((unsigned char)ch) || ch == '_' || ch == '$') {
            int startLine = line, startCol = col;
            SB id; sb_init(&id);
            while (i < len && (isalnum((unsigned char)PEEK(0)) || PEEK(0) == '_' || PEEK(0) == '$' || PEEK(0) == '.')) {
                sb_appendf(&id, "%c", PEEK(0));
                i++; col++;
            }
            TokType type = T_ID;
            if (strcmp(id.buf, "if") == 0) type = T_IF;
            else if (strcmp(id.buf, "else") == 0) type = T_ELSE;
            else if (strcmp(id.buf, "while") == 0) type = T_WHILE;
            else if (strcmp(id.buf, "var") == 0) type = T_VAR;
            else if (strcmp(id.buf, "function") == 0) type = T_FUNCTION;
            else if (strcmp(id.buf, "return") == 0) type = T_RETURN;
            else if (strcmp(id.buf, "continue") == 0) type = T_CONTINUE;
            else if (strcmp(id.buf, "break") == 0) type = T_BREAK;
            else if (strcmp(id.buf, "true") == 0) type = T_TRUE;
            else if (strcmp(id.buf, "false") == 0) type = T_FALSE;
            else if (strcmp(id.buf, "null") == 0) type = T_NULL;
            else if (strcmp(id.buf, "undefined") == 0) type = T_UNDEFINED;
            tl_add(&tl, type, id.buf, startLine, startCol);
            sb_free(&id);
            continue;
        }

        /* two-char operators */
        if (ch == '=' && PEEK(1) == '=') { i += 2; col += 2; tl_add(&tl, T_EQ, NULL, line, col - 2); continue; }
        if (ch == '=' && PEEK(1) == '>') { i += 2; col += 2; tl_add(&tl, T_ARROW, NULL, line, col - 2); continue; }
        if (ch == '<' && PEEK(1) == '=') { i += 2; col += 2; tl_add(&tl, T_LE, NULL, line, col - 2); continue; }
        if (ch == '>' && PEEK(1) == '=') { i += 2; col += 2; tl_add(&tl, T_GE, NULL, line, col - 2); continue; }
        if (ch == '|' && PEEK(1) == '|') { i += 2; col += 2; tl_add(&tl, T_OR, NULL, line, col - 2); continue; }
        if (ch == '&' && PEEK(1) == '&') { i += 2; col += 2; tl_add(&tl, T_AND, NULL, line, col - 2); continue; }
        if (ch == '+' && PEEK(1) == '+') { i += 2; col += 2; tl_add(&tl, T_INC, NULL, line, col - 2); continue; }
        if (ch == '-' && PEEK(1) == '-') { i += 2; col += 2; tl_add(&tl, T_DEC, NULL, line, col - 2); continue; }

        /* single-char punctuation / operators */
        TokType stype = T_EOF;
        switch (ch) {
            case '(': stype = T_LPAREN;   parenDepth++;   break;
            case ')': stype = T_RPAREN;   parenDepth--; if (parenDepth < 0) parenDepth = 0; break;
            case '[': stype = T_LBRACKET; bracketDepth++; break;
            case ']': stype = T_RBRACKET; bracketDepth--; if (bracketDepth < 0) bracketDepth = 0; break;
            case '{': stype = T_LBRACE;   braceDepth++;   break;
            case '}': stype = T_RBRACE;   braceDepth--; if (braceDepth < 0) braceDepth = 0; break;
            case ';': stype = T_SEMI;     break;
            case ',': stype = T_COMMA;    break;
            case ':': stype = T_COLON;    break;
            case '?': stype = T_QUESTION; break;
            case '!': stype = T_NOT;      break;
            case '+': stype = T_PLUS;     break;
            case '-': stype = T_MINUS;    break;
            case '*': stype = T_MUL;      break;
            case '/': stype = T_DIV;      break;
            case '%': stype = T_MOD;      break;
            case '=': stype = T_ASSIGN;   break;
            case '^': stype = T_POW;      break;
            case '<': stype = T_LT;       break;
            case '>': stype = T_GT;       break;
        }
        if (stype != T_EOF) {
            tl_add(&tl, stype, NULL, line, col);
            i++; col++;
            continue;
        }

        lex_error(line, col, "非法字符 \"%c\"", ch);
    }

    tl_add(&tl, T_EOF, NULL, line, col);
    *out_count = tl.n;
    return tl.data;

#undef PEEK
}

/* ======================== Parser ======================== */

typedef enum {
    N_PROGRAM, N_BLOCK,
    N_ID, N_NUMBER, N_STRING, N_BOOL, N_NULL, N_UNDEFINED,
    N_LIST, N_CALL, N_INDEX,
    N_UNARY, N_BINARY, N_TERNARY, N_ASSIGN,
    N_VARDEF, N_FUNCDEF, N_LAMBDA,
    N_IF, N_WHILE, N_RETURN,
    N_CONTINUE, N_BREAK,
    N_PREINC, N_POSTINC, N_PREDEC, N_POSTDEC
} NodeType;

typedef struct Node Node;
struct Node {
    NodeType type;
    char *value;                 /* id/number/string/bool/op/varname/funcname */
    Node **items;
    int n_items;
    int cap_items;
    Node *left;
    Node *right;
    Node *cond;
    Node *trueBranch;
    Node *falseBranch;
    Node *expr;
    Node *obj;
    Node *idx;
    char **params;
    int n_params;
    int cap_params;
};

static Node *new_node(NodeType type) {
    Node *n = (Node *)calloc(1, sizeof(Node));
    n->type = type;
    return n;
}

typedef struct {
    Node **data;
    int n;
    int cap;
} NodeList;

static void nl_init(NodeList *l) {
    l->data = NULL;
    l->n = 0;
    l->cap = 0;
}

static void nl_add(NodeList *l, Node *n) {
    if (l->n + 1 > l->cap) {
        l->cap = l->cap ? l->cap * 2 : 4;
        l->data = (Node **)realloc(l->data, l->cap * sizeof(Node *));
    }
    l->data[l->n++] = n;
}

typedef struct {
    char **data;
    int n;
    int cap;
} ParamList;

static void pl_init(ParamList *p) {
    p->data = NULL;
    p->n = 0;
    p->cap = 0;
}

static void pl_add(ParamList *p, const char *s) {
    if (p->n + 1 > p->cap) {
        p->cap = p->cap ? p->cap * 2 : 4;
        p->data = (char **)realloc(p->data, p->cap * sizeof(char *));
    }
    p->data[p->n++] = xstrdup(s);
}

static Token *tokens;
static int pos;

static Token *peek(void) { return &tokens[pos]; }
static int at_end(void) { return peek()->type == T_EOF; }
static Token *previous(void) { return &tokens[pos - 1]; }
static Token *advance_tok(void) { if (!at_end()) pos++; return previous(); }

static int match(TokType t) {
    if (peek()->type == t) { pos++; return 1; }
    return 0;
}

static int match_any(int n, ...) {
    va_list ap;
    va_start(ap, n);
    for (int i = 0; i < n; i++) {
        TokType t = va_arg(ap, TokType);
        if (peek()->type == t) { pos++; va_end(ap); return t; }
    }
    va_end(ap);
    return T_EOF;
}

static Token *expect(TokType t) {
    if (peek()->type != t) {
        Token *tok = peek();
        fprintf(stderr, "语法错误 @ %d:%d: 期望 \"%d\"，得到 \"%d\"\n",
                tok->line, tok->col, t, tok->type);
        exit(1);
    }
    return advance_tok();
}

static void parse_error(const char *fmt, ...) {
    Token *tok = peek();
    va_list ap;
    va_start(ap, fmt);
    fprintf(stderr, "语法错误 @ %d:%d: ", tok->line, tok->col);
    vfprintf(stderr, fmt, ap);
    fprintf(stderr, "\n");
    va_end(ap);
    exit(1);
}

/* Forward declarations */
static Node *parse_expr(void);
static Node *parse_term(void);
static Node *parse_unary(void);
static Node *parse_postfix(void);
static Node *parse_primary(void);
static Node *parse_paren_or_lambda(void);
static Node *parse_block(void);

static NodeList parse_expr_seq(TokType stop);
static ParamList parse_param_seq(void);
static NodeList parse_term_seq(TokType stop);

static Node *parse_ternary_from(Node *left);
static Node *parse_or_from(Node *left);
static Node *parse_and_from(Node *left);
static Node *parse_cmp_from(Node *left);
static Node *parse_add_from(Node *left);
static Node *parse_mul_from(Node *left);
static Node *parse_exp_from(Node *left);

static Node *parse_and(void);
static Node *parse_cmp(void);
static Node *parse_add(void);
static Node *parse_mul(void);
static Node *parse_exp(void);

static Node *new_binary(const char *op, Node *l, Node *r) {
    Node *n = new_node(N_BINARY);
    n->value = xstrdup(op);
    n->left = l;
    n->right = r;
    return n;
}

static Node *parse_program(void) {
    Node *n = new_node(N_PROGRAM);
    NodeList body = parse_expr_seq(T_EOF);
    expect(T_EOF);
    n->items = body.data;
    n->n_items = body.n;
    n->cap_items = body.cap;
    return n;
}

static NodeList parse_expr_seq(TokType stop) {
    NodeList list;
    nl_init(&list);
    while (peek()->type != stop && peek()->type != T_EOF) {
        if (match(T_SEMI)) continue;
        Node *e = parse_expr();
        if (e) nl_add(&list, e);
        while (match(T_SEMI)) {}
    }
    return list;
}

static Node *parse_block(void) {
    expect(T_LBRACE);
    NodeList body = parse_expr_seq(T_RBRACE);
    expect(T_RBRACE);
    Node *n = new_node(N_BLOCK);
    n->items = body.data;
    n->n_items = body.n;
    n->cap_items = body.cap;
    return n;
}

static Node *parse_function(void) {
    advance_tok(); /* function */
    Token *name = expect(T_ID);
    expect(T_LPAREN);
    ParamList params = parse_param_seq();
    expect(T_RPAREN);
    expect(T_LBRACE);
    NodeList body = parse_expr_seq(T_RBRACE);
    expect(T_RBRACE);
    Node *n = new_node(N_FUNCDEF);
    n->value = xstrdup(name->value);
    n->params = params.data;
    n->n_params = params.n;
    n->cap_params = params.cap;
    n->items = body.data;
    n->n_items = body.n;
    n->cap_items = body.cap;
    return n;
}

static Node *parse_if(void) {
    advance_tok(); /* if */
    expect(T_LPAREN);
    Node *cond = parse_term();
    expect(T_RPAREN);
    Node *thenBranch = parse_block();
    Node *elseBranch = NULL;
    while (match(T_SEMI)) {}
    if (match(T_ELSE)) {
        elseBranch = (peek()->type == T_IF) ? parse_if() : parse_block();
    }
    Node *n = new_node(N_IF);
    n->cond = cond;
    n->trueBranch = thenBranch;
    n->falseBranch = elseBranch;
    return n;
}

static Node *parse_while(void) {
    advance_tok(); /* while */
    expect(T_LPAREN);
    Node *cond = parse_term();
    expect(T_RPAREN);
    Node *body = parse_block();
    Node *n = new_node(N_WHILE);
    n->cond = cond;
    n->expr = body;
    return n;
}

static Node *parse_vardef(void) {
    advance_tok(); /* var */
    Token *name = expect(T_ID);
    Node *value = new_node(N_UNDEFINED);
    if (match(T_ASSIGN)) value = parse_term();
    Node *n = new_node(N_VARDEF);
    n->value = xstrdup(name->value);
    n->right = value;
    return n;
}

static Node *parse_return(void) {
    advance_tok(); /* return */
    Node *n = new_node(N_RETURN);
    TokType t = peek()->type;
    if (t == T_SEMI || t == T_RBRACE || t == T_EOF) return n;
    n->expr = parse_term();
    return n;
}

static ParamList parse_param_seq(void) {
    ParamList p;
    pl_init(&p);
    if (peek()->type == T_ID) {
        pl_add(&p, advance_tok()->value);
        while (match(T_COMMA)) {
            pl_add(&p, expect(T_ID)->value);
        }
    }
    return p;
}

static NodeList parse_term_seq(TokType stop) {
    NodeList list;
    nl_init(&list);
    if (peek()->type != stop) {
        while (1) {
            nl_add(&list, parse_term());
            if (match(T_COMMA)) continue;
            break;
        }
    }
    expect(stop);
    return list;
}

static Node *parse_expr(void) {
    switch (peek()->type) {
        case T_LBRACE:    return parse_block();
        case T_FUNCTION:  return parse_function();
        case T_IF:        return parse_if();
        case T_WHILE:     return parse_while();
        case T_VAR:       return parse_vardef();
        case T_RETURN:    return parse_return();
        case T_CONTINUE:  advance_tok(); return new_node(N_CONTINUE);
        case T_BREAK:     advance_tok(); return new_node(N_BREAK);
        default:          return parse_term();
    }
}

static Node *parse_term(void) {
    TokType t = peek()->type;
    if (t == T_NOT || t == T_MINUS || t == T_INC || t == T_DEC) {
        Node *u = parse_unary();
        return parse_ternary_from(u);
    }
    Node *left = parse_postfix();
    if (match(T_ASSIGN)) {
        Node *right = parse_term();
        Node *n = new_node(N_ASSIGN);
        n->left = left;
        n->right = right;
        return n;
    }
    return parse_ternary_from(left);
}

static Node *parse_ternary_from(Node *left) {
    Node *cond = parse_or_from(left);
    if (match(T_QUESTION)) {
        Node *trueBranch = parse_term();
        expect(T_COLON);
        Node *falseBranch = parse_term();
        Node *n = new_node(N_TERNARY);
        n->cond = cond;
        n->trueBranch = trueBranch;
        n->falseBranch = falseBranch;
        return n;
    }
    return cond;
}

static Node *parse_or_from(Node *left) {
    Node *node = parse_and_from(left);
    while (match(T_OR)) {
        node = new_binary("or", node, parse_and());
    }
    return node;
}
static Node *parse_and_from(Node *left) {
    Node *node = parse_cmp_from(left);
    while (match(T_AND)) {
        node = new_binary("and", node, parse_cmp());
    }
    return node;
}
static const char *cmp_op(TokType t) {
    switch (t) {
        case T_GT: return ">";
        case T_LT: return "<";
        case T_EQ: return "==";
        case T_LE: return "<=";
        case T_GE: return ">=";
        default: return "?";
    }
}
static Node *parse_cmp_from(Node *left) {
    Node *node = parse_add_from(left);
    TokType op;
    while ((op = match_any(5, T_GT, T_LT, T_EQ, T_LE, T_GE)) != T_EOF) {
        node = new_binary(cmp_op(op), node, parse_add());
    }
    return node;
}
static Node *parse_add_from(Node *left) {
    Node *node = parse_mul_from(left);
    TokType op;
    while ((op = match_any(2, T_PLUS, T_MINUS)) != T_EOF) {
        const char *s = (op == T_PLUS) ? "+" : "-";
        node = new_binary(s, node, parse_mul());
    }
    return node;
}
static Node *parse_mul_from(Node *left) {
    Node *node = parse_exp_from(left);
    TokType op;
    while ((op = match_any(3, T_MUL, T_DIV, T_MOD)) != T_EOF) {
        const char *s;
        if (op == T_MOD) s = "mod";
        else if (op == T_MUL) s = "*";
        else s = "/";
        node = new_binary(s, node, parse_exp());
    }
    return node;
}
static Node *parse_exp_from(Node *left) {
    if (match(T_POW)) {
        return new_binary("pow", left, parse_exp());
    }
    return left;
}

static Node *parse_and(void) { return parse_and_from(parse_cmp()); }
static Node *parse_cmp(void) { return parse_cmp_from(parse_add()); }
static Node *parse_add(void) { return parse_add_from(parse_mul()); }
static Node *parse_mul(void) { return parse_mul_from(parse_exp()); }
static Node *parse_exp(void) { return parse_exp_from(parse_unary()); }

static Node *parse_unary(void) {
    if (match(T_NOT)) {
        Node *n = new_node(N_UNARY);
        n->value = xstrdup("not");
        n->expr = parse_unary();
        return n;
    }
    if (match(T_MINUS)) {
        Node *e = parse_unary();
        if (e->type == N_NUMBER && e->value[0] != '-') {
            size_t len = strlen(e->value);
            char *v = (char *)malloc(len + 2);
            v[0] = '-';
            strcpy(v + 1, e->value);
            free(e->value);
            e->value = v;
            return e;
        }
        Node *n = new_node(N_UNARY);
        n->value = xstrdup("-");
        n->expr = e;
        return n;
    }
    if (match(T_INC)) {
        Node *n = new_node(N_PREINC);
        n->expr = parse_unary();
        return n;
    }
    if (match(T_DEC)) {
        Node *n = new_node(N_PREDEC);
        n->expr = parse_unary();
        return n;
    }
    return parse_postfix();
}

static Node *parse_postfix(void) {
    Node *node = parse_primary();
    while (1) {
        if (match(T_LPAREN)) {
            Node *call = new_node(N_CALL);
            call->expr = node;
            NodeList args = parse_term_seq(T_RPAREN);
            call->items = args.data;
            call->n_items = args.n;
            call->cap_items = args.cap;
            node = call;
        } else if (match(T_LBRACKET)) {
            Node *idx = new_node(N_INDEX);
            idx->obj = node;
            idx->idx = parse_term();
            expect(T_RBRACKET);
            node = idx;
        } else if (match(T_INC)) {
            Node *n = new_node(N_POSTINC);
            n->expr = node;
            node = n;
        } else if (match(T_DEC)) {
            Node *n = new_node(N_POSTDEC);
            n->expr = node;
            node = n;
        } else {
            break;
        }
    }
    return node;
}

static Node *parse_primary(void) {
    Token *tok = peek();
    switch (tok->type) {
        case T_ID: {
            advance_tok();
            Node *n = new_node(N_ID);
            n->value = xstrdup(tok->value);
            return n;
        }
        case T_NUM: {
            advance_tok();
            Node *n = new_node(N_NUMBER);
            n->value = xstrdup(tok->value);
            return n;
        }
        case T_STR: {
            advance_tok();
            Node *n = new_node(N_STRING);
            n->value = xstrdup(tok->value);
            return n;
        }
        case T_TRUE:  advance_tok(); { Node *n = new_node(N_BOOL); n->value = xstrdup("true"); return n; }
        case T_FALSE: advance_tok(); { Node *n = new_node(N_BOOL); n->value = xstrdup("false"); return n; }
        case T_NULL:      advance_tok(); return new_node(N_NULL);
        case T_UNDEFINED: advance_tok(); return new_node(N_UNDEFINED);
        case T_LBRACKET: {
            advance_tok();
            NodeList items = parse_term_seq(T_RBRACKET);
            Node *n = new_node(N_LIST);
            n->items = items.data;
            n->n_items = items.n;
            n->cap_items = items.cap;
            return n;
        }
        case T_LPAREN:
            return parse_paren_or_lambda();
        default:
            parse_error("意外的 \"%s\"", tok->value ? tok->value : "");
            return NULL;
    }
}

static Node *parse_paren_or_lambda(void) {
    int saved = pos;
    expect(T_LPAREN);
    ParamList params = parse_param_seq();
    if (peek()->type == T_RPAREN && tokens[pos + 1].type == T_ARROW) {
        advance_tok(); /* ) */
        advance_tok(); /* => */
        expect(T_LBRACE);
        NodeList body = parse_expr_seq(T_RBRACE);
        expect(T_RBRACE);
        Node *lam = new_node(N_LAMBDA);
        lam->params = params.data;
        lam->n_params = params.n;
        lam->cap_params = params.cap;
        lam->items = body.data;
        lam->n_items = body.n;
        lam->cap_items = body.cap;
        return lam;
    }
    /* not a lambda: backtrack and parse parenthesised expression */
    pos = saved;
    expect(T_LPAREN);
    Node *expr = parse_term();
    expect(T_RPAREN);
    return expr;
}

/* ======================== Emitter helpers ======================== */

static void emit_node(Node *node, SB *sb);

typedef struct {
    char **strs;
    int n;
} StrList;

static StrList emit_children(Node **items, int n_items) {
    StrList sl;
    sl.strs = (char **)malloc((n_items > 0 ? n_items : 1) * sizeof(char *));
    sl.n = 0;
    for (int i = 0; i < n_items; i++) {
        SB t; sb_init(&t);
        emit_node(items[i], &t);
        if (t.len > 0) {
            sl.strs[sl.n++] = t.buf;
        } else {
            sb_free(&t);
        }
    }
    return sl;
}

static void free_strlist(StrList *sl) {
    for (int i = 0; i < sl->n; i++) free(sl->strs[i]);
    free(sl->strs);
    sl->strs = NULL;
    sl->n = 0;
}

static void emit_params(Node *n, SB *sb) {
    for (int i = 0; i < n->n_params; i++) {
        if (i) sb_append(sb, " ");
        sb_append(sb, n->params[i]);
    }
}

static void emit_incdec(Node *n, SB *sb, const char *op) {
    sb_append(sb, "(set! ");
    emit_node(n->expr, sb);
    sb_append(sb, " (");
    sb_append(sb, op);
    sb_append(sb, " ");
    emit_node(n->expr, sb);
    sb_append(sb, " 1))");
}

/* ======================== Emitter ======================== */

static void emit_node(Node *node, SB *sb) {
    if (!node) return;
    switch (node->type) {
        case N_PROGRAM: {
            StrList terms = emit_children(node->items, node->n_items);
            sb_append(sb, "((lambda ()");
            if (terms.n > 0) {
                sb_append(sb, "\n  ");
                for (int i = 0; i < terms.n; i++) {
                    sb_append(sb, terms.strs[i]);
                    if (i + 1 < terms.n) sb_append(sb, "\n  ");
                }
            }
            sb_append(sb, "))\n");
            free_strlist(&terms);
            break;
        }
        case N_BLOCK: {
            StrList parts = emit_children(node->items, node->n_items);
            if (parts.n == 0) {
                sb_append(sb, "{}");
            } else {
                sb_append(sb, "{");
                for (int i = 0; i < parts.n; i++) {
                    sb_append(sb, parts.strs[i]);
                    if (i + 1 < parts.n) sb_append(sb, " ");
                }
                sb_append(sb, "}");
            }
            free_strlist(&parts);
            break;
        }
        case N_ID:       sb_append(sb, node->value); break;
        case N_NUMBER:   sb_append(sb, node->value); break;
        case N_STRING:   sb_append(sb, "\""); sb_append(sb, node->value); sb_append(sb, "\""); break;
        case N_BOOL:     sb_append(sb, node->value); break;
        case N_NULL:     sb_append(sb, "null"); break;
        case N_UNDEFINED:sb_append(sb, "undefined"); break;
        case N_LIST: {
            if (node->n_items == 0) {
                sb_append(sb, "'()");
            } else {
                sb_append(sb, "'(");
                for (int i = 0; i < node->n_items; i++) {
                    if (i) sb_append(sb, " ");
                    emit_node(node->items[i], sb);
                }
                sb_append(sb, ")");
            }
            break;
        }
        case N_CALL: {
            sb_append(sb, "(");
            emit_node(node->expr, sb);
            for (int i = 0; i < node->n_items; i++) {
                sb_append(sb, " ");
                emit_node(node->items[i], sb);
            }
            sb_append(sb, ")");
            break;
        }
        case N_INDEX: {
            sb_append(sb, "(get_item ");
            emit_node(node->obj, sb);
            sb_append(sb, " ");
            emit_node(node->idx, sb);
            sb_append(sb, ")");
            break;
        }
        case N_UNARY: {
            sb_append(sb, "(");
            sb_append(sb, node->value);
            sb_append(sb, " ");
            emit_node(node->expr, sb);
            sb_append(sb, ")");
            break;
        }
        case N_BINARY: {
            sb_append(sb, "(");
            sb_append(sb, node->value);
            sb_append(sb, " ");
            emit_node(node->left, sb);
            sb_append(sb, " ");
            emit_node(node->right, sb);
            sb_append(sb, ")");
            break;
        }
        case N_TERNARY: {
            sb_append(sb, "(if ");
            emit_node(node->cond, sb);
            sb_append(sb, " ");
            emit_node(node->trueBranch, sb);
            sb_append(sb, " ");
            emit_node(node->falseBranch, sb);
            sb_append(sb, ")");
            break;
        }
        case N_ASSIGN: {
            Node *rhs = node->right;
            if (node->left->type == N_INDEX) {
                sb_append(sb, "(set_item! ");
                emit_node(node->left->obj, sb);
                sb_append(sb, " ");
                emit_node(node->left->idx, sb);
                sb_append(sb, " ");
                emit_node(rhs, sb);
                sb_append(sb, ")");
            } else {
                sb_append(sb, "(set! ");
                emit_node(node->left, sb);
                sb_append(sb, " ");
                emit_node(rhs, sb);
                sb_append(sb, ")");
            }
            break;
        }
        case N_VARDEF: {
            sb_append(sb, "(define ");
            sb_append(sb, node->value);
            sb_append(sb, " ");
            emit_node(node->right, sb);
            sb_append(sb, ")");
            break;
        }
        case N_FUNCDEF: {
            sb_append(sb, "(define ");
            sb_append(sb, node->value);
            sb_append(sb, " (lambda (");
            emit_params(node, sb);
            sb_append(sb, ")");
            StrList body = emit_children(node->items, node->n_items);
            if (body.n > 0) {
                sb_append(sb, " ");
                for (int i = 0; i < body.n; i++) {
                    sb_append(sb, body.strs[i]);
                    if (i + 1 < body.n) sb_append(sb, " ");
                }
            }
            sb_append(sb, "))");
            free_strlist(&body);
            break;
        }
        case N_LAMBDA: {
            sb_append(sb, "(lambda (");
            emit_params(node, sb);
            sb_append(sb, ")");
            StrList body = emit_children(node->items, node->n_items);
            if (body.n > 0) {
                sb_append(sb, " ");
                for (int i = 0; i < body.n; i++) {
                    sb_append(sb, body.strs[i]);
                    if (i + 1 < body.n) sb_append(sb, " ");
                }
            }
            sb_append(sb, ")");
            free_strlist(&body);
            break;
        }
        case N_IF: {
            sb_append(sb, "(if ");
            emit_node(node->cond, sb);
            sb_append(sb, " ");
            emit_node(node->trueBranch, sb);
            if (node->falseBranch) {
                sb_append(sb, " ");
                emit_node(node->falseBranch, sb);
            }
            sb_append(sb, ")");
            break;
        }
        case N_WHILE: {
            sb_append(sb, "(while ");
            emit_node(node->cond, sb);
            sb_append(sb, " ");
            emit_node(node->expr, sb);
            sb_append(sb, ")");
            break;
        }
        case N_RETURN: {
            if (node->expr) emit_node(node->expr, sb);
            break;
        }
        case N_CONTINUE: sb_append(sb, "continue"); break;
        case N_BREAK:    sb_append(sb, "break"); break;
        case N_PREINC:
        case N_POSTINC:  emit_incdec(node, sb, "+"); break;
        case N_PREDEC:
        case N_POSTDEC:  emit_incdec(node, sb, "-"); break;
    }
}

/* ======================== I/O ======================== */

/* ======================== wchar_t <-> UTF-8 helpers ======================== */

static char *wcs_to_utf8(const wchar_t *src) {
    size_t len = 0;
    for (const wchar_t *p = src; *p; p++) {
        unsigned int c = (unsigned int)*p;
        if (c < 0x80) len++;
        else if (c < 0x800) len += 2;
        else if (c < 0x10000) len += 3;
        else len += 4;
    }
    char *out = (char *)malloc(len + 1);
    size_t j = 0;
    for (const wchar_t *p = src; *p; p++) {
        unsigned int c = (unsigned int)*p;
        if (c < 0x80) {
            out[j++] = (char)c;
        } else if (c < 0x800) {
            out[j++] = (char)(0xC0 | (c >> 6));
            out[j++] = (char)(0x80 | (c & 0x3F));
        } else if (c < 0x10000) {
            out[j++] = (char)(0xE0 | (c >> 12));
            out[j++] = (char)(0x80 | ((c >> 6) & 0x3F));
            out[j++] = (char)(0x80 | (c & 0x3F));
        } else {
            out[j++] = (char)(0xF0 | (c >> 18));
            out[j++] = (char)(0x80 | ((c >> 12) & 0x3F));
            out[j++] = (char)(0x80 | ((c >> 6) & 0x3F));
            out[j++] = (char)(0x80 | (c & 0x3F));
        }
    }
    out[j] = '\0';
    return out;
}

static wchar_t *utf8_to_wcs(const char *src) {
    size_t len = 0;
    for (const unsigned char *p = (const unsigned char *)src; *p; ) {
        unsigned char c = *p;
        if (c < 0x80) p++;
        else if ((c & 0xE0) == 0xC0) p += 2;
        else if ((c & 0xF0) == 0xE0) p += 3;
        else if ((c & 0xF8) == 0xF0) p += 4;
        else p++;
        len++;
    }
    wchar_t *out = (wchar_t *)malloc((len + 1) * sizeof(wchar_t));
    size_t j = 0;
    for (const unsigned char *p = (const unsigned char *)src; *p; ) {
        unsigned int code = 0;
        unsigned char c = *p;
        if (c < 0x80) {
            code = c; p++;
        } else if ((c & 0xE0) == 0xC0 && (p[1] & 0xC0) == 0x80) {
            code = ((c & 0x1F) << 6) | (p[1] & 0x3F);
            p += 2;
        } else if ((c & 0xF0) == 0xE0 && (p[1] & 0xC0) == 0x80 && (p[2] & 0xC0) == 0x80) {
            code = ((c & 0x0F) << 12) | ((p[1] & 0x3F) << 6) | (p[2] & 0x3F);
            p += 3;
        } else if ((c & 0xF8) == 0xF0 && (p[1] & 0xC0) == 0x80 && (p[2] & 0xC0) == 0x80 && (p[3] & 0xC0) == 0x80) {
            code = ((c & 0x07) << 18) | ((p[1] & 0x3F) << 12) | ((p[2] & 0x3F) << 6) | (p[3] & 0x3F);
            p += 4;
        } else {
            code = 0xFFFD; p++;
        }
        out[j++] = (wchar_t)code;
    }
    out[j] = L'\0';
    return out;
}

/* ======================== Public API ======================== */

static char *translate_utf8(const char *js_source) {
    int ntok;
    tokens = tokenize(js_source, &ntok);
    pos = 0;
    Node *ast = parse_program();
    SB out;
    sb_init(&out);
    emit_node(ast, &out);
    return out.buf;
}

/* 将 JS 代码字符串翻译成 Scheme 代码字符串。
 * 输入/输出均为宽字符字符串；返回的指针由调用者负责 free。 */
wchar_t *js_to_scheme(const wchar_t *js_source) {
    char *utf8_in = wcs_to_utf8(js_source);
    char *utf8_out = translate_utf8(utf8_in);
    free(utf8_in);
    wchar_t *out = utf8_to_wcs(utf8_out);
    free(utf8_out);
    return out;
}

static char *read_all(FILE *f) {
    size_t cap = 4096, len = 0;
    char *buf = (char *)malloc(cap);
    int c;
    while ((c = fgetc(f)) != EOF) {
        if (len + 1 >= cap) {
            cap *= 2;
            buf = (char *)realloc(buf, cap);
        }
        buf[len++] = (char)c;
    }
    buf[len] = '\0';
    return buf;
}

int main(int argc, char **argv) {
    FILE *in = stdin;
    if (argc > 1) {
        in = fopen(argv[1], "r");
        if (!in) {
            fprintf(stderr, "无法打开文件: %s\n", argv[1]);
            return 1;
        }
    }

    char *source = read_all(in);
    if (in != stdin) fclose(in);

    /* 模拟外部模块集成：先把字节流转成 wchar_t，调用 js_to_scheme，再转回 UTF-8 输出 */
    wchar_t *wjs = utf8_to_wcs(source);
    free(source);
    wchar_t *wout = js_to_scheme(wjs);
    free(wjs);
    char *out = wcs_to_utf8(wout);
    printf("%s", out);
    free(out);
    free(wout);
    /* NOTE: not freeing AST/tokens on normal exit to keep the code short. */
    return 0;
}
