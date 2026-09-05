#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "chunk.h"
#include "common.h"
#include "compiler.h"
#include "memory.h"
#include "object.h"
#include "scanner.h"

/*
 * Single-pass Pratt parser + function compiler (Milestones 2-3).
 *
 * Expressions emit bytecode on the fly without an AST. Statements
 * (`print`, `var`, blocks, `if`/`else`, `while`, `and`/`or`, `fun`,
 * `return`, calls) build on per-function chunks with jump backpatching.
 *
 * Scoping: globals are name constants (OP_DEFINE/GET/SET_GLOBAL); locals
 * are frame-relative stack slots (OP_GET/SET_LOCAL); captured locals become
 * upvalues (OP_GET/SET_UPVALUE via OP_CLOSURE descriptors). Slot 0 in every
 * function is reserved (empty name) for the VM's own use.
 */

typedef struct {
  Token current;
  Token previous;
  bool hadError;
  bool panicMode;
} Parser;

/* Precedence levels, lowest to highest. Order matters for comparisons. */
typedef enum {
  PREC_NONE,
  PREC_ASSIGNMENT, /* = */
  PREC_OR,         /* or */
  PREC_AND,        /* and */
  PREC_EQUALITY,   /* == != */
  PREC_COMPARISON, /* < > <= >= */
  PREC_TERM,       /* + - */
  PREC_FACTOR,     /* * / */
  PREC_UNARY,      /* ! - (prefix) */
  PREC_CALL,       /* () */
  PREC_PRIMARY
} Precedence;

typedef void (*ParseFn)(bool canAssign);

typedef struct {
  ParseFn prefix;
  ParseFn infix;
  Precedence precedence;
} ParseRule;

typedef struct {
  Token name;
  int depth;
  bool isCaptured;
} Local;

typedef struct {
  uint8_t index;
  bool isLocal;
} Upvalue;

typedef enum {
  TYPE_FUNCTION,
  TYPE_SCRIPT
} FunctionType;

typedef struct Compiler {
  struct Compiler* enclosing;
  ObjFunction* function;
  FunctionType type;
  Local locals[UINT8_COUNT];
  int localCount;
  Upvalue upvalues[UINT8_COUNT];
  int scopeDepth;
} Compiler;

static Parser parser;
static Compiler* current = NULL;

static Chunk* currentChunk(void) {
  return &current->function->chunk;
}

/* --- Error reporting --------------------------------------------------- */

static void errorAt(Token* token, const char* message) {
  if (parser.panicMode) return;
  parser.panicMode = true;
  fprintf(stderr, "[line %d] Error", token->line);

  if (token->type == TOKEN_EOF) {
    fprintf(stderr, " at end");
  } else if (token->type == TOKEN_ERROR) {
    /* Nothing more to say; lexeme already holds the message. */
  } else {
    fprintf(stderr, " at '%.*s'", token->length, token->start);
  }

  fprintf(stderr, ": %s\n", message);
  parser.hadError = true;
}

static void error(const char* message) {
  errorAt(&parser.previous, message);
}

static void errorAtCurrent(const char* message) {
  errorAt(&parser.current, message);
}

/* --- Scanner driver ---------------------------------------------------- */

static void advance(void) {
  parser.previous = parser.current;

  for (;;) {
    parser.current = scanToken();
    if (parser.current.type != TOKEN_ERROR) break;
    errorAtCurrent(parser.current.start);
  }
}

static void consume(TokenType type, const char* message) {
  if (parser.current.type == type) {
    advance();
    return;
  }
  errorAtCurrent(message);
}

static bool check(TokenType type) {
  return parser.current.type == type;
}

static bool match(TokenType type) {
  if (!check(type)) return false;
  advance();
  return true;
}

/* --- Bytecode emission ------------------------------------------------- */

static void emitByte(uint8_t byte) {
  writeChunk(currentChunk(), byte, parser.previous.line);
}

static void emitBytes(uint8_t byte1, uint8_t byte2) {
  emitByte(byte1);
  emitByte(byte2);
}

static void emitReturn(void) {
  emitByte(OP_NIL);
  emitByte(OP_RETURN);
}

static uint8_t makeConstant(Value value) {
  int constant = addConstant(currentChunk(), value);
  /* addConstant() exits(70) on pool overflow; this guard is belt-and-braces
   * for compilers where it might return an error instead. */
  if (constant > UINT8_MAX) {
    fprintf(stderr, "Too many constants in one chunk.\n");
    exit(70);
  }
  return (uint8_t)constant;
}

static void emitConstant(Value value) {
  emitBytes(OP_CONSTANT, makeConstant(value));
}

/* --- Jump backpatching -------------------------------------------------- */

static int emitJump(uint8_t instruction) {
  emitByte(instruction);
  emitByte(0xff);
  emitByte(0xff);
  return currentChunk()->count - 2;
}

static void patchJump(int offset) {
  /* -2 adjusts for the two operand bytes themselves. */
  int jump = currentChunk()->count - offset - 2;
  if (jump > UINT16_MAX) {
    error("Too much code to jump over.");
  }
  currentChunk()->code[offset] = (uint8_t)((jump >> 8) & 0xff);
  currentChunk()->code[offset + 1] = (uint8_t)(jump & 0xff);
}

static void emitLoop(int loopStart) {
  emitByte(OP_LOOP);
  int offset = currentChunk()->count - loopStart + 2;
  if (offset > UINT16_MAX) {
    error("Loop body too large.");
  }
  emitByte((uint8_t)((offset >> 8) & 0xff));
  emitByte((uint8_t)(offset & 0xff));
}

/* --- Forward declarations for Pratt table ------------------------------ */

static void expression(void);
static void statement(void);
static void declaration(void);
static ParseRule* getRule(TokenType type);
static void parsePrecedence(Precedence precedence);
static uint8_t identifierConstant(Token* name);
static int resolveLocal(Compiler* compiler, Token* name);
static int resolveUpvalue(Compiler* compiler, Token* name);
static void namedVariable(Token name, bool canAssign);
static void block(void);
static void beginScope(void);
static void endScope(void);
static void function(FunctionType type);
static void funDeclaration(void);

/* --- Prefix / infix parse functions ------------------------------------ */

static void grouping(bool canAssign) {
  (void)canAssign;
  expression();
  consume(TOKEN_RIGHT_PAREN, "Expect ')' after expression.");
}

static void number(bool canAssign) {
  (void)canAssign;
  /* The lexeme is a prefix of the null-terminated source buffer, so strtod()
   * stops at the first non-numeric char (operator, whitespace, '\0'). The
   * scanner already rejected malformed literals like "123abc". */
  double value = strtod(parser.previous.start, NULL);
  emitConstant(NUMBER_VAL(value));
}

static void string(bool canAssign) {
  (void)canAssign;
  /* Token includes surrounding quotes; unescape the interior into a heap
   * buffer (via reallocate, never malloc) and intern as an ObjString. */
  const char* start = parser.previous.start + 1;
  int innerLength = parser.previous.length - 2;

  char* heap = ALLOCATE(char, innerLength + 1);
  int out = 0;
  for (int i = 0; i < innerLength; i++) {
    char c = start[i];
    if (c == '\\' && i + 1 < innerLength) {
      char e = start[i + 1];
      switch (e) {
        case '"':
          heap[out++] = '"';
          break;
        case '\\':
          heap[out++] = '\\';
          break;
        case 'n':
          heap[out++] = '\n';
          break;
        case 't':
          heap[out++] = '\t';
          break;
        case 'r':
          heap[out++] = '\r';
          break;
        case '0':
          heap[out++] = '\0';
          break;
        default:
          /* Unknown escape: keep the escaped char, drop the backslash. */
          heap[out++] = e;
          break;
      }
      i++; /* consumed the escaped char */
    } else {
      heap[out++] = c;
    }
  }
  heap[out] = '\0';
  emitConstant(OBJ_VAL(takeString(heap, out)));
}

static void literal(bool canAssign) {
  (void)canAssign;
  switch (parser.previous.type) {
    case TOKEN_FALSE:
      emitByte(OP_FALSE);
      break;
    case TOKEN_NIL:
      emitByte(OP_NIL);
      break;
    case TOKEN_TRUE:
      emitByte(OP_TRUE);
      break;
    default:
      return; /* Unreachable. */
  }
}

static void unary(bool canAssign) {
  (void)canAssign;
  TokenType operatorType = parser.previous.type;

  /* Compile the operand with strictly higher precedence so `-a.b` and
   * `!!x` nest correctly. */
  parsePrecedence(PREC_UNARY);

  switch (operatorType) {
    case TOKEN_MINUS:
      emitByte(OP_NEGATE);
      break;
    case TOKEN_BANG:
      emitByte(OP_NOT);
      break;
    default:
      return; /* Unreachable. */
  }
}

static void binary(bool canAssign) {
  (void)canAssign;
  TokenType operatorType = parser.previous.type;
  ParseRule* rule = getRule(operatorType);
  /* Right operand binds tighter (left-associative): `a - b - c` parses as
   * `(a - b) - c`. */
  parsePrecedence((Precedence)(rule->precedence + 1));

  switch (operatorType) {
    case TOKEN_BANG_EQUAL:
      emitBytes(OP_EQUAL, OP_NOT);
      break;
    case TOKEN_EQUAL_EQUAL:
      emitByte(OP_EQUAL);
      break;
    case TOKEN_GREATER:
      emitByte(OP_GREATER);
      break;
    case TOKEN_GREATER_EQUAL:
      /* a >= b  <=>  !(a < b) */
      emitBytes(OP_LESS, OP_NOT);
      break;
    case TOKEN_LESS:
      emitByte(OP_LESS);
      break;
    case TOKEN_LESS_EQUAL:
      /* a <= b  <=>  !(a > b) */
      emitBytes(OP_GREATER, OP_NOT);
      break;
    case TOKEN_PLUS:
      emitByte(OP_ADD);
      break;
    case TOKEN_MINUS:
      emitByte(OP_SUBTRACT);
      break;
    case TOKEN_STAR:
      emitByte(OP_MULTIPLY);
      break;
    case TOKEN_SLASH:
      emitByte(OP_DIVIDE);
      break;
    default:
      return; /* Unreachable. */
  }
}

static void variable(bool canAssign) {
  namedVariable(parser.previous, canAssign);
}

static void and_(bool canAssign) {
  (void)canAssign;
  /* `a and b`: if a is falsy, leave it as the result; else pop it and
   * evaluate b. JUMP_IF_FALSE peeks so the short-circuited value stays. */
  int endJump = emitJump(OP_JUMP_IF_FALSE);
  emitByte(OP_POP);
  parsePrecedence(PREC_AND);
  patchJump(endJump);
}

static void or_(bool canAssign) {
  (void)canAssign;
  /* `a or b`: if a is falsy, discard it and evaluate b; else skip b. */
  int elseJump = emitJump(OP_JUMP_IF_FALSE);
  int endJump = emitJump(OP_JUMP);
  patchJump(elseJump);
  emitByte(OP_POP);
  parsePrecedence(PREC_OR);
  patchJump(endJump);
}

static uint8_t argumentList(void) {
  uint8_t argCount = 0;
  if (!check(TOKEN_RIGHT_PAREN)) {
    do {
      expression();
      if (argCount == 255) {
        error("Can't have more than 255 arguments.");
      }
      argCount++;
    } while (match(TOKEN_COMMA));
  }
  consume(TOKEN_RIGHT_PAREN, "Expect ')' after arguments.");
  return argCount;
}

static void call(bool canAssign) {
  (void)canAssign;
  uint8_t argCount = argumentList();
  emitBytes(OP_CALL, argCount);
}

/* --- Pratt table -------------------------------------------------------- */

static ParseRule rules[] = {
  [TOKEN_LEFT_PAREN]    = {grouping, call,   PREC_CALL},
  [TOKEN_RIGHT_PAREN]   = {NULL,     NULL,   PREC_NONE},
  [TOKEN_LEFT_BRACE]    = {NULL,     NULL,   PREC_NONE},
  [TOKEN_RIGHT_BRACE]   = {NULL,     NULL,   PREC_NONE},
  [TOKEN_COMMA]         = {NULL,     NULL,   PREC_NONE},
  [TOKEN_DOT]           = {NULL,     NULL,   PREC_NONE},
  [TOKEN_MINUS]         = {unary,    binary, PREC_TERM},
  [TOKEN_PLUS]          = {NULL,     binary, PREC_TERM},
  [TOKEN_SEMICOLON]     = {NULL,     NULL,   PREC_NONE},
  [TOKEN_SLASH]         = {NULL,     binary, PREC_FACTOR},
  [TOKEN_STAR]          = {NULL,     binary, PREC_FACTOR},
  [TOKEN_BANG]          = {unary,    NULL,   PREC_NONE},
  [TOKEN_BANG_EQUAL]    = {NULL,     binary, PREC_EQUALITY},
  [TOKEN_EQUAL]         = {NULL,     NULL,   PREC_NONE},
  [TOKEN_EQUAL_EQUAL]   = {NULL,     binary, PREC_EQUALITY},
  [TOKEN_GREATER]       = {NULL,     binary, PREC_COMPARISON},
  [TOKEN_GREATER_EQUAL] = {NULL,     binary, PREC_COMPARISON},
  [TOKEN_LESS]          = {NULL,     binary, PREC_COMPARISON},
  [TOKEN_LESS_EQUAL]    = {NULL,     binary, PREC_COMPARISON},
  [TOKEN_IDENTIFIER]    = {variable, NULL,   PREC_NONE},
  [TOKEN_STRING]        = {string,   NULL,   PREC_NONE},
  [TOKEN_NUMBER]        = {number,   NULL,   PREC_NONE},
  [TOKEN_AND]           = {NULL,     and_,   PREC_AND},
  [TOKEN_ELSE]          = {NULL,     NULL,   PREC_NONE},
  [TOKEN_FALSE]         = {literal,  NULL,   PREC_NONE},
  [TOKEN_FUN]           = {NULL,     NULL,   PREC_NONE},
  [TOKEN_IF]            = {NULL,     NULL,   PREC_NONE},
  [TOKEN_NIL]           = {literal,  NULL,   PREC_NONE},
  [TOKEN_OR]            = {NULL,     or_,    PREC_OR},
  [TOKEN_PRINT]         = {NULL,     NULL,   PREC_NONE},
  [TOKEN_RETURN]        = {NULL,     NULL,   PREC_NONE},
  [TOKEN_TRUE]          = {literal,  NULL,   PREC_NONE},
  [TOKEN_VAR]           = {NULL,     NULL,   PREC_NONE},
  [TOKEN_WHILE]         = {NULL,     NULL,   PREC_NONE},
  [TOKEN_ERROR]         = {NULL,     NULL,   PREC_NONE},
  [TOKEN_EOF]           = {NULL,     NULL,   PREC_NONE},
};

static ParseRule* getRule(TokenType type) {
  return &rules[type];
}

static void parsePrecedence(Precedence precedence) {
  advance();
  ParseFn prefixRule = getRule(parser.previous.type)->prefix;
  if (prefixRule == NULL) {
    error("Expect expression.");
    return;
  }

  bool canAssign = precedence <= PREC_ASSIGNMENT;
  prefixRule(canAssign);

  while (precedence <= getRule(parser.current.type)->precedence) {
    advance();
    ParseFn infixRule = getRule(parser.previous.type)->infix;
    infixRule(canAssign);
  }

  if (canAssign && match(TOKEN_EQUAL)) {
    error("Invalid assignment target.");
  }
}

/* --- Variables, upvalues and scoping ------------------------------------- */

static bool identifiersEqual(Token* a, Token* b) {
  if (a->length != b->length) return false;
  return memcmp(a->start, b->start, (size_t)a->length) == 0;
}

static int resolveLocal(Compiler* compiler, Token* name) {
  for (int i = compiler->localCount - 1; i >= 0; i--) {
    Local* local = &compiler->locals[i];
    if (identifiersEqual(name, &local->name)) {
      if (local->depth == -1) {
        error("Can't read local variable in its own initializer.");
      }
      return i;
    }
  }
  return -1;
}

static int addUpvalue(Compiler* compiler, uint8_t index, bool isLocal) {
  int upvalueCount = compiler->function->upvalueCount;

  for (int i = 0; i < upvalueCount; i++) {
    Upvalue* upvalue = &compiler->upvalues[i];
    if (upvalue->index == index && upvalue->isLocal == isLocal) {
      return i;
    }
  }

  if (upvalueCount == UINT8_COUNT) {
    error("Too many closure variables in function.");
    return 0;
  }

  compiler->upvalues[upvalueCount].isLocal = isLocal;
  compiler->upvalues[upvalueCount].index = index;
  return compiler->function->upvalueCount++;
}

static int resolveUpvalue(Compiler* compiler, Token* name) {
  if (compiler->enclosing == NULL) return -1;

  int local = resolveLocal(compiler->enclosing, name);
  if (local != -1) {
    compiler->enclosing->locals[local].isCaptured = true;
    return addUpvalue(compiler, (uint8_t)local, true);
  }

  int upvalue = resolveUpvalue(compiler->enclosing, name);
  if (upvalue != -1) {
    return addUpvalue(compiler, (uint8_t)upvalue, false);
  }

  return -1;
}

static void addLocal(Token name) {
  if (current->localCount == UINT8_COUNT) {
    error("Too many local variables in function.");
    return;
  }
  Local* local = &current->locals[current->localCount++];
  local->name = name;
  local->depth = -1;
  local->isCaptured = false;
}

static void declareVariable(void) {
  if (current->scopeDepth == 0) return;

  Token* name = &parser.previous;
  for (int i = current->localCount - 1; i >= 0; i--) {
    Local* local = &current->locals[i];
    if (local->depth != -1 && local->depth < current->scopeDepth) break;
    if (identifiersEqual(name, &local->name)) {
      error("Already a variable with this name in this scope.");
    }
  }
  addLocal(*name);
}

static uint8_t identifierConstant(Token* name) {
  return makeConstant(OBJ_VAL(copyString(name->start, name->length)));
}

static uint8_t parseVariable(const char* errorMessage) {
  consume(TOKEN_IDENTIFIER, errorMessage);
  declareVariable();
  if (current->scopeDepth > 0) return 0;
  return identifierConstant(&parser.previous);
}

static void markInitialized(void) {
  if (current->scopeDepth == 0) return;
  current->locals[current->localCount - 1].depth = current->scopeDepth;
}

static void defineVariable(uint8_t global) {
  if (current->scopeDepth > 0) {
    markInitialized();
    return;
  }
  emitBytes(OP_DEFINE_GLOBAL, global);
}

static void namedVariable(Token name, bool canAssign) {
  uint8_t getOp;
  uint8_t setOp;
  int arg = resolveLocal(current, &name);
  if (arg != -1) {
    getOp = OP_GET_LOCAL;
    setOp = OP_SET_LOCAL;
  } else if ((arg = resolveUpvalue(current, &name)) != -1) {
    getOp = OP_GET_UPVALUE;
    setOp = OP_SET_UPVALUE;
  } else {
    arg = identifierConstant(&name);
    getOp = OP_GET_GLOBAL;
    setOp = OP_SET_GLOBAL;
  }

  if (canAssign && match(TOKEN_EQUAL)) {
    expression();
    emitBytes(setOp, (uint8_t)arg);
  } else {
    emitBytes(getOp, (uint8_t)arg);
  }
}

/* --- Statements ---------------------------------------------------------- */

static void expression(void) {
  parsePrecedence(PREC_ASSIGNMENT);
}

static void block(void) {
  while (!check(TOKEN_RIGHT_BRACE) && !check(TOKEN_EOF)) {
    declaration();
  }
  consume(TOKEN_RIGHT_BRACE, "Expect '}' after block.");
}

static void beginScope(void) {
  current->scopeDepth++;
}

static void endScope(void) {
  current->scopeDepth--;
  while (current->localCount > 0 &&
         current->locals[current->localCount - 1].depth >
             current->scopeDepth) {
    if (current->locals[current->localCount - 1].isCaptured) {
      emitByte(OP_CLOSE_UPVALUE);
    } else {
      emitByte(OP_POP);
    }
    current->localCount--;
  }
}

static void printStatement(void) {
  expression();
  consume(TOKEN_SEMICOLON, "Expect ';' after value.");
  emitByte(OP_PRINT);
}

static void expressionStatement(void) {
  expression();
  consume(TOKEN_SEMICOLON, "Expect ';' after expression.");
  emitByte(OP_POP);
}

static void ifStatement(void) {
  consume(TOKEN_LEFT_PAREN, "Expect '(' after 'if'.");
  expression();
  consume(TOKEN_RIGHT_PAREN, "Expect ')' after condition.");

  int thenJump = emitJump(OP_JUMP_IF_FALSE);
  emitByte(OP_POP);
  statement();

  int elseJump = emitJump(OP_JUMP);
  patchJump(thenJump);
  emitByte(OP_POP);

  if (match(TOKEN_ELSE)) statement();
  patchJump(elseJump);
}

static void whileStatement(void) {
  int loopStart = currentChunk()->count;
  consume(TOKEN_LEFT_PAREN, "Expect '(' after 'while'.");
  expression();
  consume(TOKEN_RIGHT_PAREN, "Expect ')' after condition.");

  int exitJump = emitJump(OP_JUMP_IF_FALSE);
  emitByte(OP_POP);
  statement();
  emitLoop(loopStart);

  patchJump(exitJump);
  emitByte(OP_POP);
}

static void returnStatement(void) {
  if (current->type == TYPE_SCRIPT) {
    error("Can't return from top-level code.");
  }

  if (match(TOKEN_SEMICOLON)) {
    emitReturn();
  } else {
    expression();
    consume(TOKEN_SEMICOLON, "Expect ';' after return value.");
    emitByte(OP_RETURN);
  }
}

static void varDeclaration(void) {
  uint8_t global = parseVariable("Expect variable name.");

  if (match(TOKEN_EQUAL)) {
    expression();
  } else {
    emitByte(OP_NIL);
  }
  consume(TOKEN_SEMICOLON, "Expect ';' after variable declaration.");

  defineVariable(global);
}

static void function(FunctionType type) {
  Compiler compiler;
  /* initCompiler */
  compiler.enclosing = current;
  compiler.function = NULL;
  compiler.type = type;
  compiler.localCount = 0;
  compiler.scopeDepth = 0;
  compiler.function = newFunction();
  current = &compiler;
  if (type != TYPE_SCRIPT) {
    current->function->name =
        copyString(parser.previous.start, parser.previous.length);
  }

  Local* local = &current->locals[current->localCount++];
  local->depth = 0;
  local->isCaptured = false;
  local->name.start = "";
  local->name.length = 0;

  beginScope();

  consume(TOKEN_LEFT_PAREN, "Expect '(' after function name.");
  if (!check(TOKEN_RIGHT_PAREN)) {
    do {
      current->function->arity++;
      if (current->function->arity > 255) {
        errorAtCurrent("Can't have more than 255 parameters.");
      }
      uint8_t constant = parseVariable("Expect parameter name.");
      defineVariable(constant);
    } while (match(TOKEN_COMMA));
  }
  consume(TOKEN_RIGHT_PAREN, "Expect ')' after parameters.");
  consume(TOKEN_LEFT_BRACE, "Expect '{' before function body.");
  block();

  ObjFunction* function = NULL;
  /* endCompiler */
  emitReturn();
  function = current->function;
  current = current->enclosing;

  emitBytes(OP_CLOSURE, makeConstant(OBJ_VAL(function)));
  for (int i = 0; i < function->upvalueCount; i++) {
    emitByte(compiler.upvalues[i].isLocal ? 1 : 0);
    emitByte(compiler.upvalues[i].index);
  }
}

static void funDeclaration(void) {
  uint8_t global = parseVariable("Expect function name.");
  markInitialized();
  function(TYPE_FUNCTION);
  defineVariable(global);
}

static void statement(void) {
  if (match(TOKEN_PRINT)) {
    printStatement();
  } else if (match(TOKEN_IF)) {
    ifStatement();
  } else if (match(TOKEN_RETURN)) {
    returnStatement();
  } else if (match(TOKEN_WHILE)) {
    whileStatement();
  } else if (match(TOKEN_LEFT_BRACE)) {
    beginScope();
    block();
    endScope();
  } else {
    expressionStatement();
  }
}

static void synchronize(void) {
  parser.panicMode = false;

  while (parser.current.type != TOKEN_EOF) {
    if (parser.previous.type == TOKEN_SEMICOLON) return;

    switch (parser.current.type) {
      case TOKEN_PRINT:
      case TOKEN_VAR:
      case TOKEN_FUN:
      case TOKEN_IF:
      case TOKEN_WHILE:
      case TOKEN_RETURN:
        return;
      default:
        break;
    }

    advance();
  }
}

static void declaration(void) {
  if (match(TOKEN_FUN)) {
    funDeclaration();
  } else if (match(TOKEN_VAR)) {
    varDeclaration();
  } else {
    statement();
  }

  if (parser.panicMode) synchronize();
}

/* --- Entry point -------------------------------------------------------- */

ObjFunction* compile(const char* source) {
  initScanner(source);

  Compiler compiler;
  compiler.enclosing = NULL;
  compiler.function = NULL;
  compiler.type = TYPE_SCRIPT;
  compiler.localCount = 0;
  compiler.scopeDepth = 0;
  compiler.function = newFunction();
  current = &compiler;

  parser.hadError = false;
  parser.panicMode = false;

  advance();

  while (!match(TOKEN_EOF)) {
    declaration();
  }

  ObjFunction* function = NULL;
  emitReturn();
  function = current->function;
  current = current->enclosing;

  return parser.hadError ? NULL : function;
}
