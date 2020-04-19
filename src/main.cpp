#include "als.h"

#include "ast.h"
#include "ast_print.h"
#include "error.h"
#include "parse.h"
#include "print.h"
#include "resolve.h"
#include "scan.h"

#include <stdio.h>

int main()
{
	// TODO: Read file in from command line

#if 1
	// Desktop setup

	char * filename = "W:/Meek/examples/test.meek";
#else
	// Laptop setup

	char * filename = "C:/Users/Andrew/Desktop/clylang/test.meek";		// TODO: Read this in from command line
#endif

	// TODO: Support files bigger than 1 mb. Maybe have scanner return a special value when its buffer is full and it will ask you to pass it a new buffer?

	int bufferSize = 1024 * 1024;

	char * buffer = new char[bufferSize];
	Defer(delete[] buffer);

	FILE * file = fopen(filename, "rb");
	Defer(if (file) fclose(file));

	int bytesRead = (int)(file ? fread(buffer, 0x1, bufferSize, file) : -1);

	if (bytesRead < 0)
	{
		// TODO: print to stderr

		print("Error opening file\n");
		return 1;
	}
	else if (bytesRead == bufferSize)
	{
		// TOOD: print to stderr

		printfmt("Files larger than %d bytes are not yet supported", bufferSize);
		return 1;
	}

	Scanner scanner;
	init(&scanner, buffer, bytesRead);

	Parser parser;
	init(&parser, &scanner);

	AstNode * pAstRoot = nullptr;
	{
		bool success;
		pAstRoot = parseProgram(&parser, &success);

		if (!success)
		{
			// TODO: Still try to do semantic analysis on non-erroneous parts of the program so that we can report better errors?

			reportScanAndParseErrors(parser);
			return 1;
		}
	}

	if (!tryResolveAllTypes(&parser))
	{
		print("Unable to resolve some types\n");
		return 1;
	}

	// TODO (andrew) Probably just eagerly insert func names into the symbol table like we do for others, and generate types pending resolution
	//	that will poke in the typid's of the args. Then, this function could be a simple audit to make sure that there are no redefined funcs.

	//if (!tryResolvePendingFuncSymbolsAfterTypesResolved(&parser.symbTable))
	//{
	//	print("Couldn't resolve all func symbols\n");
	//	return 1;
	//}

	ResolvePassCtx resolvePass;
	init(&resolvePass, &parser);
	doResolvePass(&resolvePass, pAstRoot);

	print("Resolve pass all done\n");

	println();
	println();
	/*debugPrintSymbolTable(parser.symbTable);*/

	println();
	debugPrintTypeTable(parser.typeTable);

#if DEBUG && 0
	DebugPrintCtx debugPrintCtx;
	init(&debugPrintCtx.mpLevelSkip);
	debugPrintCtx.pTypeTable = &parser.typeTable;

	debugPrintAst(&debugPrintCtx, *pAstRoot);


	println();
	if (parser.apErrorNodes.cItem > 0)
	{
		print("Parse had error(s)!\n");
	}
	else
	{
		print("No parse errors :)\n");
	}

#endif
}
