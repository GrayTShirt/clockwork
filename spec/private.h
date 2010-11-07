#ifndef _SPEC_PRIVATE_H
#define _SPEC_PRIVATE_H

/* This headers is "private" and includes structure definitions
   and function prototypes that are ONLY of interest to the
   internals of the lexer and grammar modules, spec/lexer.o
   and spec/grammar.o.
 */

#include "../ast.h"
#include "../stringlist.h"

/**
  parser_branch - interim representation of an if block

  This structure is used internally by the Bison grammar to store
  information needed to construct an if-then-else form, prior to
  converting it into a string of syntax tree nodes.  This specific
  implementation is limited to 'if fact is [ v1, v2 ]' idioms.

  The 'affirmative' member is a boolean.  If it is set to 1, the
  if block contains an affirmative test; the 'then' block represents
  the control path to follow if the value of fact is contained in
  the values string list and the 'otherwise' block is the control path
  to follow if it is not.

  If affirmative is set to 0, the test is taken as a negation of
  'fact is [list]'.  The then and otherwise pointers have their
  meaning switched; if the value of fact is contained in the values
  string list, 'otherwise' is followed.  If not, 'then' is followed.
 */
typedef struct {
	const char     *fact;         /* Name of fact to test */
	stringlist     *values;       /* List of values to check */
	unsigned char   affirmative;  /* see above */
	struct ast     *then;         /* The 'then' node, used in syntax tree conversion */
	struct ast     *otherwise;    /* The 'else' node, used in syntax tree conversion */
} parser_branch;

/**
  parser_map - interim representation of a map construct

  This structure is used internally by the Bison grammar to store
  information needed to build a map(fact) { "v1": "value" } construct,
  prior to converting it into a string of syntax tree nodes.

  The two string lists, fact_values and attr_values, are parallel;
  the second string in attr_values is the value to set the attribute
  to if the second string in fact_values is the value of the fact.

  If default value is NULL, no else clause was parsed, and the attribute
  will not be set.
 */
typedef struct {
	const char     *fact;          /* Name of fact to test */
	const char     *attribute;     /* Name of attribute to set */

	stringlist     *fact_values;   /* List of values to check */
	stringlist     *attr_values;   /* List of values to set attribute to */

	const char     *default_value; /* Default value (else clause) for attribute */
} parser_map;

/**
  spec_parser_context - User data for reentrant lexer / parser

  This structure bundles a bunch of variables that we need to keep
  track of while lexing and parsing.  An instance of this structure
  gets passed to yyparse, which propagates it through to yylex.

  The scanner member is an opaque pointer, allocated by yylex_init and
  freed by yylex_destroy.  It contains whatever Flex needs it to, and is
  used in place of global variables (like yylineno, yyleng, and friends).

  The root member will be an abstract syntax tree node with an op of
  AST_OP_PROG (progression).  This node will contain syntax trees of all
  policy generators and host definitions found while parsing.
 */
typedef struct {
	void *scanner;    /* lexer variable store; used instead of globals */

	struct ast *root; /* root node of the syntax tree parsed */
	void *buf1;       /* Current buffer in use by yylex */

	const char *file; /* Name of the current file being parsed */
} spec_parser_context;

#define YY_EXTRA_TYPE spec_parser_context*
typedef union {
	char           *string;
	stringlist     *strings;
	struct ast     *node;
	stringlist     *string_hash[2];
	char           *string_pair[2];

	parser_branch  *branch;
	parser_map     *map;
} YYSTYPE;
#define YYSTYPE_IS_DECLARED 1

#ifndef YY_TYPEDEF_YY_SCANNER_T
#define YY_TYPEDEF_YY_SCANNER_T
typedef void* yyscan_t;
#endif

/**
  Set up parser and lexer parameters.

  YYPARSE_PARAM contains the name of the user data parameter
  of yyparse.  Bison will generate C code like this:

    void yyparse(void *YYPARSE_PARAM)

  This allow us to pass our spec_parser_context pointer around
  and keep track of the lexer scanner state.

  YYLEX_PARAM is defined as a way to extract the lexer state
  variable (the scanner member of spec_parser_context) so that
  it can be passed to yylex.  Flex doesn't care about the
  spec_parser_context structure at all.
 */
#define YYPARSE_PARAM ctx
#define YYLEX_PARAM   ((spec_parser_context*)ctx)->scanner

/* Defined in lexer.c */
int yylex(YYSTYPE*, yyscan_t);
int yylex_init_extra(YY_EXTRA_TYPE, yyscan_t*);
int yylex_destroy(yyscan_t);
void yyset_extra(YY_EXTRA_TYPE, yyscan_t);
int yyparse(void*);

/* Defined in lexer.l */
void parse_error(const char *error, void *user_data);
/* Define yyerror as a macro that invokes parse_error

   Because the Flex-generated C code contains calls to
   yyerror with the following signature:

     void yyerror(const char*)

   This is the cleanest way to get error reporting with
   line numbers and other useful information.  It is
   worth pointing out that this definition will only work
   if yyerror is called from yylex; otherwise the macro
   expansion of YYPARSE_PARAM is potentially invalid (there
   may not be a ctx variable to dereference).  Sine Flex
   only ever calls yyerror from within yylex, this assumption
   is safe insofar as generated code is concerned.
 */
#define yyerror(s) parse_error(s, YYPARSE_PARAM)

/* Create a new YY_BUFFER_STATE object and set it up to
   read from the FILE pointer given. */
int lexer_new_buffer(FILE*, spec_parser_context*);

#endif