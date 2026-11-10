#include <stdio.h>
#include <string.h>

#include "common.h"
#include "scanner.h"

typedef struct {
  const char* start;
  const char* current;
  int line;
} Scanner;

Scanner scanner;

static bool is_at_end() {
  return *scanner.current == '\0';
}

static Token make_token(TokenType type) {
  Token token;
  token.type = type;
  token.start = scanner.start;
  token.length = (int) (scanner.current - scanner.start);
  token.line = scanner.line;
  return token;
}

static Token error_token(const char* message) {
  Token token;
  token.type = TOKEN_ERROR;
  token.start = message;
  token.length = (int) strlen(message);
  token.line = scanner.line;
  return token;
}

// advances to the next character and returns the previous one
static char advance() {
  scanner.current++;
  return scanner.current[-1];
}

// checks if current char is equal to a specific value
static bool match(char expected) {
  if (is_at_end()) return false;
  if (*scanner.current != expected) return false;
  scanner.current++;
  return true;
}

// checks the value of the current character without advancing
static char peek() {
  return *scanner.current;
}

// checks the value of the next character without advancing
static char peek_next() {
  if (is_at_end()) return '\0';
  return scanner.current[1];
}

static void skip_whitespace() {
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
        if (peek_next() == '/') {
          // a comment goes until the end of the line.
          while (peek() != '\n' && !is_at_end()) advance();
        } else {
          return;
        }
        break;
      default:
        return;
    }
  }
}

static Token make_string() {
  // skip chars until another quote char is found
  while (peek() != '"' && !is_at_end()) {
    if (peek() == '\n') scanner.line++;
    advance();
  }

  if (is_at_end()) return error_token("Unterminated string");

  // skip closing quote
  advance();
  return make_token(TOKEN_STRING);
}

static bool is_digit(char c) {
  return c >= '0' && c <= '9';
}

static Token make_number() {
  while (is_digit(peek())) advance();

  if (peek() == '.' && is_digit(peek_next())) {
    // consume dot
    advance();

    while (is_digit(peek())) advance();
  }

  return make_token(TOKEN_NUMBER);
}

static bool is_alpha(char c) {
  return (c >= 'a' && c <= 'z') ||
         (c >= 'A' && c <= 'Z') ||
          c == '_';
}

static TokenType check_keyword(int start, int length,
    const char* rest, TokenType type) {
  if (scanner.current - scanner.start == start + length &&
      memcmp(scanner.start + start, rest, length) == 0) {
    return type;
  }

  return TOKEN_IDENTIFIER;
}

// contextual keywords:
// keywords that are reserved in some contexts but not in others
// e.g. await inside an async block in C#ifndef FILE_H
// pros:
// they are only not keywords most of the time
// less annoying for users who would be otherwise forced to change the name of an identifier
// cons:
// more difficult syntax highlighting (requires grammar instead of simple regex)
// how to implement:
// distinguish contextual keyword tokens (e.g. TOKEN_AWAIT)
// then let the parser accept them as regular identifiers if required
static TokenType identifier_type() {
  switch (scanner.start[0]) {
    case 'a': return check_keyword(1, 2, "nd", TOKEN_AND);
    case 'c': {
      if (scanner.current - scanner.start > 1) {
        switch (scanner.start[1]) {
          case 'l': return check_keyword(2, 3, "ass", TOKEN_CLASS);
          case 'o': return check_keyword(2, 3, "nst", TOKEN_CONST);
        }
      }
    }
    case 'e': return check_keyword(1, 3, "lse", TOKEN_ELSE);
    case 'f':
      if (scanner.current - scanner.start > 1) {
        switch (scanner.start[1]) {
          case 'a': return check_keyword(2, 3, "lse", TOKEN_FALSE);
          case 'o': return check_keyword(2, 1, "r", TOKEN_FOR);
          case 'u': return check_keyword(2, 1, "n", TOKEN_FUN);
        }
      }
      break;
    case 'i': return check_keyword(1, 1, "f", TOKEN_IF);
    case 'n': return check_keyword(1, 2, "il", TOKEN_NIL);
    case 'o': return check_keyword(1, 1, "r", TOKEN_OR);
    case 'p': return check_keyword(1, 4, "rint", TOKEN_PRINT);
    case 'r': return check_keyword(1, 5, "eturn", TOKEN_RETURN);
    case 's': return check_keyword(1, 4, "uper", TOKEN_SUPER);
    case 't':
      if (scanner.current - scanner.start > 1) {
        switch (scanner.start[1]) {
          case 'h': return check_keyword(2, 2, "is", TOKEN_THIS);
          case 'r': return check_keyword(2, 2, "ue", TOKEN_TRUE);
        }
      }
      break;
    case 'v': return check_keyword(1, 2, "ar", TOKEN_VAR);
    case 'w': return check_keyword(1, 4, "hile", TOKEN_WHILE);
  }

  return TOKEN_IDENTIFIER;
}

static Token make_identifier() {
  while (is_alpha(peek()) ||is_digit(peek())) advance();
  return make_token(identifier_type());
}

void init_scanner(const char *source) {
  scanner.start = source;
  scanner.current = source;
  scanner.line = 1;
}

Token scan_token() {
  skip_whitespace();
  scanner.start = scanner.current;

  if (is_at_end()) return make_token(TOKEN_EOF);

  char c = advance();
  if (is_digit(c)) return make_number();
  if (is_alpha(c)) return make_identifier();

  switch (c) {
    case '(': return make_token(TOKEN_LEFT_PAREN);
    case ')': return make_token(TOKEN_RIGHT_PAREN);
    case '{': return make_token(TOKEN_LEFT_BRACE);
    case '}': return make_token(TOKEN_RIGHT_BRACE);
    case ';': return make_token(TOKEN_SEMICOLON);
    case ',': return make_token(TOKEN_COMMA);
    case '.': return make_token(TOKEN_DOT);
    case '-': return make_token(TOKEN_MINUS);
    case '+': return make_token(TOKEN_PLUS);
    case '/': return make_token(TOKEN_SLASH);
    case '*': return make_token(TOKEN_STAR);
    case '!':
      return make_token(
          match('=') ? TOKEN_BANG_EQUAL : TOKEN_BANG);
    case '=':
      return make_token(
          match('=') ? TOKEN_EQUAL_EQUAL : TOKEN_EQUAL);
    case '<':
      return make_token(
          match('=') ? TOKEN_LESS_EQUAL : TOKEN_LESS);
    case '>':
      return make_token(
          match('=') ? TOKEN_GREATER_EQUAL : TOKEN_GREATER);
    case '"':
      return make_string();
  }

  return error_token("Unexpected character");
}

