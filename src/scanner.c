#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#include "scanner.h"

/*
 * Maximal-munch, single-pass lexer. It keeps two pointers into the source --
 * `start` (beginning of the current lexeme) and `current` (scan position) --
 * plus a running line counter, and produces one Token per call to scanToken().
 * Errors are reported as TOKEN_ERROR tokens carrying the message in the
 * lexeme, so a REPL can keep going past a bad token.
 *
 * Note the scanner emits lexemes only: converting "3.14" to a double or
 * "\"hi\"" to a string object happens in the compiler (Milestone 2), which
 * keeps this layer decoupled from the runtime.
 *
 * Robustness: every <ctype.h> call casts its argument to (unsigned char),
 * which is required for non-ASCII / UTF-8 bytes (values > 127); passing a
 * negative char to isalpha()/isdigit() would be undefined behavior. String
 * literals also understand backslash escapes so an escaped quote ("\"") does
 * not terminate the string early.
 */
typedef struct {
  const char* start;
  const char* current;
  int line;
} Scanner;

static Scanner scanner;

void initScanner(const char* source) {
  scanner.start = source;
  scanner.current = source;
  scanner.line = 1;
}

static bool isAtEnd(void) {
  return *scanner.current == '\0';
}

static char advance(void) {
  scanner.current++;
  return scanner.current[-1];
}

static char peek(void) {
  return *scanner.current;
}

static char peekNext(void) {
  if (isAtEnd()) return '\0';
  return scanner.current[1];
}

/* Consume `expected` if it is next; used for two-character operators. */
static bool match(char expected) {
  if (isAtEnd()) return false;
  if (*scanner.current != expected) return false;
  scanner.current++;
  return true;
}

static Token makeToken(TokenType type) {
  Token token;
  token.type = type;
  token.start = scanner.start;
  token.length = (int)(scanner.current - scanner.start);
  token.line = scanner.line;
  return token;
}

static Token errorToken(const char* message) {
  Token token;
  token.type = TOKEN_ERROR;
  token.start = message;
  token.length = (int)strlen(message);
  token.line = scanner.line;
  return token;
}

static void skipWhitespace(void) {
  for (;;) {
    char c = peek();
    switch (c) {
      case ' ':
      case '\r':
      case '\t':
        advance();
        break;

      case '\n':
        scanner.line++;
        advance();
        break;

      case '/':
        if (peekNext() == '/') { /* line comment */
          while (peek() != '\n' && !isAtEnd()) advance();
        } else {
          return;
        }
        break;

      default:
        return;
    }
  }
}

static TokenType checkKeyword(int start, int length, const char* rest,
                              TokenType type) {
  /* Lexeme must be exactly the keyword: same total length and matching chars. */
  if (scanner.current - scanner.start == start + length &&
      memcmp(scanner.start + start, rest, (size_t)length) == 0) {
    return type;
  }
  return TOKEN_IDENTIFIER;
}

static TokenType identifierType(void) {
  switch (scanner.start[0]) {
    case 'a': return checkKeyword(1, 2, "nd", TOKEN_AND);
    case 'e': return checkKeyword(1, 3, "lse", TOKEN_ELSE);
    case 'f':
      if (scanner.current - scanner.start > 1) {
        switch (scanner.start[1]) {
          case 'a': return checkKeyword(2, 3, "lse", TOKEN_FALSE);
          case 'u': return checkKeyword(2, 1, "n", TOKEN_FUN);
        }
      }
      break;
    case 'i': return checkKeyword(1, 1, "f", TOKEN_IF);
    case 'n': return checkKeyword(1, 2, "il", TOKEN_NIL);
    case 'o': return checkKeyword(1, 1, "r", TOKEN_OR);
    case 'p': return checkKeyword(1, 4, "rint", TOKEN_PRINT);
    case 'r': return checkKeyword(1, 5, "eturn", TOKEN_RETURN);
    case 't': return checkKeyword(1, 3, "rue", TOKEN_TRUE);
    case 'v': return checkKeyword(1, 2, "ar", TOKEN_VAR);
    case 'w': return checkKeyword(1, 4, "hile", TOKEN_WHILE);
  }
  return TOKEN_IDENTIFIER;
}

static Token identifier(void) {
  while (isalpha((unsigned char)peek()) || isdigit((unsigned char)peek()) ||
         peek() == '_') {
    advance();
  }
  return makeToken(identifierType());
}

static Token number(void) {
  while (isdigit((unsigned char)peek())) advance();

  /* Fractional part: only if a '.' is followed by a digit, so "1.5.6"
   * lexes as "1.5" then ".6" instead of swallowing an error. */
  if (peek() == '.' && isdigit((unsigned char)peekNext())) {
    advance();
    while (isdigit((unsigned char)peek())) advance();
  }

  /* A number glued to an identifier character ("123abc") is one bad token,
   * not two adjacent tokens. */
  if (isalpha((unsigned char)peek()) || peek() == '_') {
    return errorToken("Invalid number literal.");
  }

  return makeToken(TOKEN_NUMBER);
}

static Token string(void) {
  while (peek() != '"' && !isAtEnd()) {
    if (peek() == '\\') {
      /* Backslash escape: skip the escaped character so an escaped quote
       * does not end the string. The lexeme keeps the raw escape sequence;
       * actual unescaping happens in the compiler (Milestone 2). */
      advance(); /* the backslash */
      if (isAtEnd()) break;
      if (peek() == '\n') scanner.line++; /* line numbers must stay correct */
      advance(); /* the escaped character */
      continue;
    }

    if (peek() == '\n') scanner.line++;
    advance();
  }

  if (isAtEnd()) return errorToken("Unterminated string.");

  advance(); /* closing quote */
  return makeToken(TOKEN_STRING);
}

Token scanToken(void) {
  skipWhitespace();
  scanner.start = scanner.current;

  if (isAtEnd()) return makeToken(TOKEN_EOF);

  char c = advance();

  if (isalpha((unsigned char)c) || c == '_') return identifier();
  if (isdigit((unsigned char)c)) return number();

  switch (c) {
    case '(': return makeToken(TOKEN_LEFT_PAREN);
    case ')': return makeToken(TOKEN_RIGHT_PAREN);
    case '{': return makeToken(TOKEN_LEFT_BRACE);
    case '}': return makeToken(TOKEN_RIGHT_BRACE);
    case ';': return makeToken(TOKEN_SEMICOLON);
    case ',': return makeToken(TOKEN_COMMA);
    case '.': return makeToken(TOKEN_DOT);
    case '-': return makeToken(TOKEN_MINUS);
    case '+': return makeToken(TOKEN_PLUS);
    case '/': return makeToken(TOKEN_SLASH);
    case '*': return makeToken(TOKEN_STAR);
    case '!': return makeToken(match('=') ? TOKEN_BANG_EQUAL : TOKEN_BANG);
    case '=': return makeToken(match('=') ? TOKEN_EQUAL_EQUAL : TOKEN_EQUAL);
    case '<': return makeToken(match('=') ? TOKEN_LESS_EQUAL : TOKEN_LESS);
    case '>': return makeToken(match('=') ? TOKEN_GREATER_EQUAL : TOKEN_GREATER);
    case '"': return string();
  }

  return errorToken("Unexpected character.");
}

const char* tokenTypeName(TokenType type) {
  switch (type) {
    case TOKEN_LEFT_PAREN:    return "LEFT_PAREN";
    case TOKEN_RIGHT_PAREN:   return "RIGHT_PAREN";
    case TOKEN_LEFT_BRACE:    return "LEFT_BRACE";
    case TOKEN_RIGHT_BRACE:   return "RIGHT_BRACE";
    case TOKEN_COMMA:         return "COMMA";
    case TOKEN_DOT:           return "DOT";
    case TOKEN_MINUS:         return "MINUS";
    case TOKEN_PLUS:          return "PLUS";
    case TOKEN_SEMICOLON:     return "SEMICOLON";
    case TOKEN_SLASH:         return "SLASH";
    case TOKEN_STAR:          return "STAR";
    case TOKEN_BANG:          return "BANG";
    case TOKEN_BANG_EQUAL:    return "BANG_EQUAL";
    case TOKEN_EQUAL:         return "EQUAL";
    case TOKEN_EQUAL_EQUAL:   return "EQUAL_EQUAL";
    case TOKEN_GREATER:       return "GREATER";
    case TOKEN_GREATER_EQUAL: return "GREATER_EQUAL";
    case TOKEN_LESS:          return "LESS";
    case TOKEN_LESS_EQUAL:    return "LESS_EQUAL";
    case TOKEN_IDENTIFIER:    return "IDENTIFIER";
    case TOKEN_STRING:        return "STRING";
    case TOKEN_NUMBER:        return "NUMBER";
    case TOKEN_AND:           return "AND";
    case TOKEN_ELSE:          return "ELSE";
    case TOKEN_FALSE:         return "FALSE";
    case TOKEN_FUN:           return "FUN";
    case TOKEN_IF:            return "IF";
    case TOKEN_NIL:           return "NIL";
    case TOKEN_OR:            return "OR";
    case TOKEN_PRINT:         return "PRINT";
    case TOKEN_RETURN:        return "RETURN";
    case TOKEN_TRUE:          return "TRUE";
    case TOKEN_VAR:           return "VAR";
    case TOKEN_WHILE:         return "WHILE";
    case TOKEN_ERROR:         return "ERROR";
    case TOKEN_EOF:           return "EOF";
  }
  return "?";
}