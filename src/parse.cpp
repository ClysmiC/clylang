#include "parse.h"

#include "error.h"
#include "scan.h"

// Absolutely sucks that I need to use 0, 1, 2 suffixes. I tried this approach to simulate default parameters in a macro but MSVC has a bug
//	with varargs expansion that made it blow up: https://stackoverflow.com/questions/3046889/optional-parameters-with-c-macros

#define AstNew(pParser, astk, startEnd) reinterpret_cast<Ast##astk *>(astNew(pParser, ASTK_##astk, startEnd))

#define AstNewErr0Child(pParser, astkErr, startEnd) reinterpret_cast<Ast##astkErr *>(astNewErr(pParser, ASTK_##astkErr, startEnd, nullptr, nullptr))
#define AstNewErr1Child(pParser, astkErr, startEnd, pChild0) reinterpret_cast<Ast##astkErr *>(astNewErr(pParser, ASTK_##astkErr, startEnd, pChild0, nullptr))
#define AstNewErr2Child(pParser, astkErr, startEnd, pChild0, pChild1) reinterpret_cast<Ast##astkErr *>(astNewErr(pParser, ASTK_##astkErr, startEnd, pChild0, pChild1))
#define AstNewErr3Child(pParser, astkErr, startEnd, pChild0, pChild1, pChild2) reinterpret_cast<Ast##astkErr *>(astNewErr(pParser, ASTK_##astkErr, startEnd, pChild0, pChild1, pChild2))

// NOTE: This macro is functionally equivalent to AstNewErr2Child since we rely on function overloading to choose the correct one.
//	I'm not really happy with this solution but it works for now.

#define AstNewErrListChild(pParser, astkErr, startEnd, apChildren, cPChildren) reinterpret_cast<Ast##astkErr *>(astNewErr(pParser, ASTK_##astkErr, startEnd, apChildren, cPChildren))
#define AstNewErrListChildMove(pParser, astkErr, startEnd, papChildren) reinterpret_cast<Ast##astkErr *>(astNewErrMoveChildren(pParser, ASTK_##astkErr, startEnd, papChildren))

static const BinopInfo s_aParseOp[] = {
	{ 0, { TOKENK_Star, TOKENK_Slash, TOKENK_Percent }, 3 },
	{ 1, { TOKENK_Plus, TOKENK_Minus }, 2 },
	{ 2, { TOKENK_Lesser, TOKENK_Greater, TOKENK_LesserEqual, TOKENK_GreaterEqual }, 4 },
	{ 3, { TOKENK_EqualEqual, TOKENK_BangEqual }, 2 },
	{ 4, { TOKENK_HashAnd }, 1 },
	{ 5, { TOKENK_HashXor }, 1 },
	{ 6, { TOKENK_HashOr }, 1 },
	{ 7, { TOKENK_AmpAmp }, 1 },
	{ 8, { TOKENK_PipePipe }, 1 },
};
static constexpr int s_iParseOpMax = ArrayLen(s_aParseOp);

bool init(Parser * pParser, Scanner * pScanner)
{
	if (!pParser || !pScanner) return false;

	pParser->pScanner = pScanner;
	init(&pParser->astAlloc);
	init(&pParser->tokenAlloc);
	init(&pParser->typeAlloc);
	// init(&pParser->astNodes);
	init(&pParser->scopeStack);
	init(&pParser->symbTable);
	init(&pParser->typeTable);
	init(&pParser->astDecs);
	init(&pParser->apErrorNodes);

	return true;
}

AstNode * parseProgram(Parser * pParser, bool * poSuccess)
{
	// NOTE: Empty program is valid to parse, but I may still decide that
	//	that is a semantic error...

	pushScope(pParser, SCOPEK_BuiltIn);
	Assert(peekScope(pParser).id == SCOPEID_BuiltIn);

	insertBuiltInTypes(&pParser->typeTable);
	insertBuiltInSymbols(&pParser->symbTable);

	pushScope(pParser, SCOPEK_Global);
	Assert(peekScope(pParser).id == SCOPEID_Global);

	DynamicArray<AstNode *> apNodes;
	init(&apNodes);
	Defer(AssertInfo(!apNodes.pBuffer, "Should be moved into program AST node"));

	StartEndIndices startEnd;
	bool isFirstNode = true;

	while (!isFinished(pParser->pScanner) && peekToken(pParser->pScanner) != TOKENK_Eof)
	{
		AstNode * pNode = parseStmt(pParser, PARSESTMTK_TopLevelStmt);

		if (isErrorNode(*pNode))
		{
			// NOTE: should move this recoverFromPanic to the parseXXXStmt functions
			//	themselves. Otherwise errors will always bubble up to here which is
			//	inconsistent with how they behave in expression contexts.

			bool recovered = tryRecoverFromPanic(pParser, TOKENK_Semicolon);

			Assert(Implies(!recovered, isFinished(pParser->pScanner)));
		}

		if (isFirstNode)
		{
			auto startEndNode = getStartEnd(pParser->astDecs, pNode->astid);
			startEnd.iStart = startEndNode.iStart;
			startEnd.iEnd = startEndNode.iEnd;
		}
		else
		{
			auto startEndNode = getStartEnd(pParser->astDecs, pNode->astid);
			startEnd.iEnd = startEndNode.iEnd;
		}

		append(&apNodes, pNode);

		isFirstNode = false;
	}

	auto * pNodeProgram = AstNew(pParser, Program, startEnd);
	initMove(&pNodeProgram->apNodes, &apNodes);
	*poSuccess = (pParser->apErrorNodes.cItem == 0);
	return Up(pNodeProgram);
}

AstNode * parseStmt(Parser * pParser, PARSESTMTK parsestmtk)
{
	// HMM: Need different syntax for const and compile time const vardecls. for struct defn's and
	//	top-level fun defn's they want to be compile time const. But maybe we can aggressively infer
	//	compile time constness and make it a semantic error if they aren't. So that way the following
	//	syntaxes would be allowed

	// Basically just need to think a lot about how fun and struct decl's look (or if they are even permitted!) in the following 3 cases
	//	-compile time immutable
	//	-runtime immutable
	//	-runtime mutable


	// Peek around a bit to figure out what kind of statement it is!

	TOKENK tokenkNext = peekToken(pParser->pScanner, nullptr, 0);
	TOKENK tokenkNextNext = peekToken(pParser->pScanner, nullptr, 1);

	if (tokenkNext == TOKENK_Struct)
	{
		// Struct defn

		auto * pNode = parseStructDefnStmt(pParser);

		if (isErrorNode(*pNode)) return pNode;

		Assert(pNode->astk == ASTK_StructDefnStmt);

		if (parsestmtk == PARSESTMTK_DoStmt)
		{
			auto * pErr = AstNewErr1Child(pParser, IllegalDoStmtErr, getStartEnd(pParser->astDecs, pNode->astid), pNode);
			pErr->astkStmt = pNode->astk;
			return Up(pErr);
		}

		return pNode;
	}
	else if (tokenkNext == TOKENK_Fn && tokenkNextNext == TOKENK_Identifier)
	{
		AstNode * pNode = parseFuncDefnStmt(pParser);

		if (isErrorNode(*pNode)) return pNode;

		Assert(pNode->astk == ASTK_FuncDefnStmt);

		// Func defn

		if (parsestmtk == PARSESTMTK_DoStmt)
		{
			auto * pErr = AstNewErr1Child(pParser, IllegalDoStmtErr, getStartEnd(pParser->astDecs, pNode->astid), pNode);
			pErr->astkStmt = pNode->astk;
			return Up(pErr);
		}

		return pNode;
	}
	//else if (tokenkNext == TOKENK_Enum && tokenkNextNext == TOKENK_Identifier)
	//{
	//	// Enum defn

	//	Assert(false);		// TODO
	//}
	//else if (tokenkNext == TOKENK_Union && tokenkNextNext == TOKENK_Identifier)
	//{
	//	// Union defn

	//	// TODO
	//}
	else if (tokenkNext == TOKENK_If)
	{
		auto * pNode = parseIfStmt(pParser);

		if (isErrorNode(*pNode)) return pNode;

		Assert(pNode->astk == ASTK_IfStmt);

		if (parsestmtk == PARSESTMTK_TopLevelStmt)
		{
			auto * pErr = AstNewErr1Child(pParser, IllegalTopLevelStmtErr, getStartEnd(pParser->astDecs, pNode->astid), pNode);
			pErr->astkStmt = pNode->astk;
			return Up(pErr);
		}

        return pNode;
	}
	else if (tokenkNext == TOKENK_While)
	{
		auto * pNode = parseWhileStmt(pParser);

		if (isErrorNode(*pNode)) return pNode;

		Assert(pNode->astk == ASTK_WhileStmt);

		if (parsestmtk == PARSESTMTK_TopLevelStmt)
		{
			auto * pErr = AstNewErr1Child(pParser, IllegalTopLevelStmtErr, getStartEnd(pParser->astDecs, pNode->astid), pNode);
			pErr->astkStmt = pNode->astk;
			return Up(pErr);
		}

        return pNode;
	}
	else if (tokenkNext == TOKENK_OpenBrace)
	{
		// Block

		auto * pNode = parseBlockStmt(pParser);

		if (isErrorNode(*pNode)) return pNode;

		Assert(pNode->astk == ASTK_BlockStmt);

		if (parsestmtk == PARSESTMTK_DoStmt)
		{
			auto * pErr = AstNewErr1Child(pParser, IllegalDoStmtErr, getStartEnd(pParser->astDecs, pNode->astid), pNode);
			pErr->astkStmt = pNode->astk;
			return Up(pErr);
		}

		if (parsestmtk == PARSESTMTK_TopLevelStmt)
		{
			auto * pErr = AstNewErr1Child(pParser, IllegalTopLevelStmtErr, getStartEnd(pParser->astDecs, pNode->astid), pNode);
			pErr->astkStmt = pNode->astk;
			return Up(pErr);
		}

		return pNode;
	}
	else if (tokenkNext == TOKENK_OpenBracket ||
			 tokenkNext == TOKENK_Carat ||
			 (tokenkNext == TOKENK_Identifier && tokenkNextNext == TOKENK_Identifier) ||
			 tokenkNext == TOKENK_Fn)
	{
		// HMM: Should I put this earlier since var decls are so common?
		//	I have to have it at least after func defn so I can be assured
		//	that if we peek "fn" that it isn't part of a func defn

		// Var decl

		auto * pNode = parseVarDeclStmt(pParser);

		if (isErrorNode(*pNode)) return pNode;

		Assert(pNode->astk == ASTK_VarDeclStmt);

		if (parsestmtk == PARSESTMTK_DoStmt)
		{
			auto * pErr = AstNewErr1Child(pParser, IllegalDoStmtErr, getStartEnd(pParser->astDecs, pNode->astid), pNode);
			pErr->astkStmt = pNode->astk;
			return Up(pErr);
		}

		return pNode;
	}
	else if (tokenkNext == TOKENK_Return)
	{
		// Return

		auto * pNode = parseReturnStmt(pParser);

		if (isErrorNode(*pNode)) return pNode;

		Assert(pNode->astk == ASTK_ReturnStmt);

		if (parsestmtk == PARSESTMTK_TopLevelStmt)
		{
			auto * pErr = AstNewErr1Child(pParser, IllegalTopLevelStmtErr, getStartEnd(pParser->astDecs, pNode->astid), pNode);
			pErr->astkStmt = pNode->astk;
			return Up(pErr);
		}

		return pNode;
	}
	else if (tokenkNext == TOKENK_Break)
	{
		// Break

		auto * pNode = parseBreakStmt(pParser);

		if (isErrorNode(*pNode)) return pNode;

		Assert(pNode->astk == ASTK_BreakStmt);

		if (parsestmtk == PARSESTMTK_TopLevelStmt)
		{
			auto * pErr = AstNewErr1Child(pParser, IllegalTopLevelStmtErr, getStartEnd(pParser->astDecs, pNode->astid), pNode);
			pErr->astkStmt = pNode->astk;
			return Up(pErr);
		}

		return pNode;
	}
	else if (tokenkNext == TOKENK_Continue)
	{
		// Continue

		auto * pNode = parseContinueStmt(pParser);

		if (isErrorNode(*pNode)) return pNode;

		Assert(pNode->astk == ASTK_ContinueStmt);

		if (parsestmtk == PARSESTMTK_TopLevelStmt)
		{
			auto * pErr = AstNewErr1Child(pParser, IllegalTopLevelStmtErr, getStartEnd(pParser->astDecs, pNode->astid), pNode);
			pErr->astkStmt = pNode->astk;
			return Up(pErr);
		}

		return pNode;
	}
	else
	{
		// Expr stmt or Assignment stmt

		auto * pNode = parseExprStmtOrAssignStmt(pParser);

		if (isErrorNode(*pNode)) return pNode;

		Assert(pNode->astk == ASTK_AssignStmt || pNode->astk == ASTK_ExprStmt);

		if (parsestmtk == PARSESTMTK_TopLevelStmt)
		{
			auto * pErr = AstNewErr1Child(pParser, IllegalTopLevelStmtErr, getStartEnd(pParser->astDecs, pNode->astid), pNode);
			pErr->astkStmt = pNode->astk;
			return Up(pErr);
		}

		return pNode;
	}
}

AstNode * parseExprStmtOrAssignStmt(Parser * pParser)
{
	// Parse lhs expression

	int iStart = peekTokenStartEnd(pParser->pScanner).iStart;

	AstNode * pLhsExpr = parseExpr(pParser);
	if (isErrorNode(*pLhsExpr))
	{
		return pLhsExpr;
	}

	// Check if it is an assignment

	bool isAssignment = false;
	AstNode * pRhsExpr = nullptr;

	const static TOKENK s_aTokenkAssign[] = {
		TOKENK_Equal,
		TOKENK_PlusEqual,
		TOKENK_MinusEqual,
		TOKENK_StarEqual,
		TOKENK_SlashEqual,
		TOKENK_PercentEqual
	};
	const static int s_cTokenkAssign = ArrayLen(s_aTokenkAssign);

	Token * pAssignToken = nullptr;
	if (tryConsumeToken(pParser->pScanner, s_aTokenkAssign, s_cTokenkAssign, ensurePendingToken(pParser)))
	{
		isAssignment = true;
		pAssignToken = claimPendingToken(pParser);

		// Parse rhs expression

		pRhsExpr = parseExpr(pParser);
		if (isErrorNode(*pRhsExpr))
		{
			auto * pErr = AstNewErr2Child(pParser, BubbleErr, gc_startEndBubble, pLhsExpr, pRhsExpr);
			return Up(pErr);
		}

		if (tryConsumeToken(pParser->pScanner, s_aTokenkAssign, s_cTokenkAssign, ensurePendingToken(pParser)))
		{
			// NOTE: This check isn't necessary for correctness, but it gives a better error message for a common case.

			auto * pErr = AstNewErr2Child(
							pParser,
							ChainedAssignErr,
							makeStartEnd(iStart, prevTokenStartEnd(pParser->pScanner).iEnd),
							pLhsExpr,
							pRhsExpr);

			return Up(pErr);
		}
	}

	// Parse semicolon

	if (!tryConsumeToken(pParser->pScanner, TOKENK_Semicolon, ensurePendingToken(pParser)))
	{
		auto startEndPrev = prevTokenStartEnd(pParser->pScanner);

		if (isAssignment)
		{
			auto * pErr = AstNewErr2Child(pParser, ExpectedTokenkErr, makeStartEnd(startEndPrev.iEnd + 1), pLhsExpr, pRhsExpr);
			append(&pErr->aTokenkValid, TOKENK_Semicolon);
			return Up(pErr);
		}
		else
		{
			auto * pErr = AstNewErr1Child(pParser, ExpectedTokenkErr, makeStartEnd(startEndPrev.iEnd + 1), pLhsExpr);
			append(&pErr->aTokenkValid, TOKENK_Semicolon);
			return Up(pErr);
		}
	}

	int iEnd = prevTokenStartEnd(pParser->pScanner).iEnd;
    auto startEnd = makeStartEnd(iStart, iEnd);

	// Success!

	if (isAssignment)
	{
		auto * pNode = AstNew(pParser, AssignStmt, startEnd);

		pNode->pAssignToken = pAssignToken;
		pNode->pLhsExpr = pLhsExpr;
		pNode->pRhsExpr = pRhsExpr;
		return Up(pNode);
	}
	else
	{
		Assert(!pRhsExpr);
		Assert(!pAssignToken);

		auto * pNode = AstNew(pParser, ExprStmt, startEnd);
		pNode->pExpr = pLhsExpr;
		return Up(pNode);
	}
}

AstNode * parseStructDefnStmt(Parser * pParser)
{
	SCOPEID declScope = peekScope(pParser).id;
	int iStart = peekTokenStartEnd(pParser->pScanner).iStart;

	// Parse 'struct'

	if (!tryConsumeToken(pParser->pScanner, TOKENK_Struct))
	{
		auto startEndPrev = prevTokenStartEnd(pParser->pScanner);

		auto * pErr = AstNewErr0Child(pParser, ExpectedTokenkErr, makeStartEnd(startEndPrev.iEnd + 1));
		append(&pErr->aTokenkValid, TOKENK_Struct);
		return Up(pErr);
	}


	// Parse identifier

	if (!tryConsumeToken(pParser->pScanner, TOKENK_Identifier, ensurePendingToken(pParser)))
	{
		auto startEndPrev = prevTokenStartEnd(pParser->pScanner);

		auto * pErr = AstNewErr0Child(pParser, ExpectedTokenkErr, makeStartEnd(startEndPrev.iEnd + 1));
		append(&pErr->aTokenkValid, TOKENK_Identifier);
		return Up(pErr);
	}

	Token * pIdentTok = claimPendingToken(pParser);
	Assert(pIdentTok->tokenk == TOKENK_Identifier);

	// Parse '{'

	if (!tryConsumeToken(pParser->pScanner, TOKENK_OpenBrace))
	{
		auto startEndPrev = prevTokenStartEnd(pParser->pScanner);

		auto * pErr = AstNewErr0Child(pParser, ExpectedTokenkErr, makeStartEnd(startEndPrev.iEnd + 1));
		append(&pErr->aTokenkValid, TOKENK_OpenBrace);
		return Up(pErr);
	}

	bool success = false;
	pushScope(pParser, SCOPEK_StructDefn);
	Defer(if (!success) popScope(pParser));		// NOTE: In success case we pop it manually before inserting into symbol table

	// TODO: Allow nested struct definitions!

	// Parse vardeclstmt list, then '}'

	DynamicArray<AstNode *> apVarDeclStmt;
	init(&apVarDeclStmt);
	Defer(Assert(!apVarDeclStmt.pBuffer));

	while (!tryConsumeToken(pParser->pScanner, TOKENK_CloseBrace))
	{
		// Parse member var decls

		AstNode * pVarDeclStmt = parseVarDeclStmt(pParser);
		append(&apVarDeclStmt, pVarDeclStmt);

		if (isErrorNode(*pVarDeclStmt))
		{
			static const TOKENK s_aTokenkRecover[] = { TOKENK_CloseBrace, TOKENK_Semicolon };
			TOKENK tokenkRecover;
			if (tryRecoverFromPanic(pParser, s_aTokenkRecover, ArrayLen(s_aTokenkRecover), &tokenkRecover))
			{
				if (tokenkRecover == TOKENK_CloseBrace)
				{
					break;
				}
				else
				{
					Assert(tokenkRecover == TOKENK_Semicolon);
					continue;
				}
			}

			Assert(apVarDeclStmt.cItem > 0);

			auto * pErr = AstNewErrListChildMove(pParser, BubbleErr, gc_startEndBubble, &apVarDeclStmt);
			return Up(pErr);
		}
	}

	int iEnd = prevTokenStartEnd(pParser->pScanner).iEnd;

	success = true;

	auto * pNode = AstNew(pParser, StructDefnStmt, makeStartEnd(iStart, iEnd));
	setIdent(&pNode->ident, pIdentTok, declScope);
	initMove(&pNode->apVarDeclStmt, &apVarDeclStmt);
	pNode->scopeid = peekScope(pParser).id;

	// Insert into symbol table of enclosing scope

	popScope(pParser);

	SymbolInfo structDefnInfo;
	setSymbolInfo(&structDefnInfo, pNode->ident, SYMBOLK_Struct, Up(pNode));
	tryInsert(&pParser->symbTable, pNode->ident, structDefnInfo, pParser->scopeStack);

	// Insert into type table

	Type type;
	init(&type, false /* isFuncType */);

	type.ident = pNode->ident;
	pNode->typidSelf = ensureInTypeTable(&pParser->typeTable, type, true /* debugAssertIfAlreadyInTable */ );

	return Up(pNode);
}

AstNode * parseVarDeclStmt(Parser * pParser, EXPECTK expectkName, EXPECTK expectkSemicolon)
{
	AssertInfo(expectkName != EXPECTK_Forbidden, "Function does not (currently) support being called with expectkName forbidden");
	AssertInfo(expectkSemicolon != EXPECTK_Optional, "Semicolon should either be required or forbidden");

	// This list already kind of exists embedded in the modifiers and func types,
	//	but we store it separately here to make it easier to attach them to an error
	//	node should we ultimately produce an error.

	DynamicArray<AstNode *> apNodeChildren;
	init(&apNodeChildren);
	Defer(dispose(&apNodeChildren););

	bool success = false;

	int iStart = peekTokenStartEnd(pParser->pScanner).iStart;

    // NOTE: Subscript expressions in array types are not directly put in the tree (or in our error children)... they are instead embedded into the type pending resolution, at which point they
    //  are evaluated and the entire type is translated into a typid. Then they are no longer needed.

	// Parse type
	TypePendingResolution * pTypePending;
	{
        AstNode * pTypeErr = nullptr;
		pTypePending = tryParseType(pParser, &pTypeErr);
		if (parseTypeResult == PARSETYPERESULT_ParseFailed)
		{
			Assert(apNodeChildren.cItem > 0);

			AstErr * pErrFromParseType;
			{
				AstNode * pNode = apNodeChildren[apNodeChildren.cItem - 1];
				Assert(category(pNode->astk) == ASTCATK_Error);
				pErrFromParseType = DownErr(pNode);
			}

			auto * pErr = AstNewErrListChildMove(pParser, BubbleErr, gc_startEndBubble, &apNodeChildren);
			return Up(pErr);
		}

		Assert(Implies(parseTypeResult == PARSETYPERESULT_ParseSucceededTypeResolveSucceeded, !pTypePending));
		Assert(Implies(parseTypeResult == PARSETYPERESULT_ParseSucceededTypeResolveFailed, pTypePending));
        Assert(Implies(parseTypeResult == PARSETYPERESULT_ParseFailedButRecovered, !pTypePending));
	}

	// Parse name

	Token * pVarIdent = nullptr;
	Defer(
		if (!success && pVarIdent) release(&pParser->tokenAlloc, pVarIdent);
	);

	if (expectkName == EXPECTK_Optional || expectkName == EXPECTK_Required)
	{
		if (tryConsumeToken(pParser->pScanner, TOKENK_Identifier, ensurePendingToken(pParser)))
		{
			pVarIdent = claimPendingToken(pParser);
			Assert(pVarIdent->tokenk == TOKENK_Identifier);
		}
		else if (expectkName == EXPECTK_Required)
		{
			auto startEndPrev = prevTokenStartEnd(pParser->pScanner);

			auto * pErr = AstNewErrListChildMove(pParser, ExpectedTokenkErr, makeStartEnd(startEndPrev.iEnd + 1), &apNodeChildren);
			append(&pErr->aTokenkValid, TOKENK_Identifier);
			return Up(pErr);
		}
	}
	else
	{
		AssertInfo(false, "Var decls can be optionally named in some contexts (func params), but it doesn't make sense to forbid naming them!");
	}

	// Parse init expr

	AstNode * pInitExpr = nullptr;

    int iEqualStart = -1;
	if (tryConsumeToken(pParser->pScanner, TOKENK_Equal))
	{
        iEqualStart = prevTokenStartEnd(pParser->pScanner).iStart;

		pInitExpr = parseExpr(pParser);
		append(&apNodeChildren, pInitExpr);

		if (isErrorNode(*pInitExpr))
		{
			auto * pErr = AstNewErrListChildMove(pParser, BubbleErr, gc_startEndBubble, &apNodeChildren);
			return Up(pErr);
		}

		if (tryConsumeToken(pParser->pScanner, TOKENK_Equal))
		{
			// NOTE: This check isn't necessary for correctness, but it gives a better error message for a common case.

			auto * pErr = AstNewErrListChildMove(
							pParser,
							ChainedAssignErr,
							makeStartEnd(iStart, prevTokenStartEnd(pParser->pScanner).iEnd),
							&apNodeChildren);

			return Up(pErr);
		}
	}

	// Providing init expr isn't allowed if the var isn't named...

	if (!pVarIdent && pInitExpr)
	{
        Assert(iEqualStart != -1);
        auto startEndPrev = prevTokenStartEnd(pParser->pScanner);

		auto * pErr = AstNewErrListChildMove(pParser, InitUnnamedVarErr, makeStartEnd(iEqualStart, startEndPrev.iEnd), &apNodeChildren);
		return Up(pErr);
	}

	// Parse semicolon

	if (expectkSemicolon == EXPECTK_Required)
	{
		if (!tryConsumeToken(pParser->pScanner, TOKENK_Semicolon, ensurePendingToken(pParser)))
		{
            auto startEndPrev = prevTokenStartEnd(pParser->pScanner);

			auto * pErr = AstNewErrListChildMove(pParser, ExpectedTokenkErr, makeStartEnd(startEndPrev.iEnd + 1), &apNodeChildren);
			append(&pErr->aTokenkValid, TOKENK_Semicolon);
			return Up(pErr);
		}
	}

	// Success!

    success = true;
    int iEnd = prevTokenStartEnd(pParser->pScanner).iEnd;

	SCOPEID declScope = peekScope(pParser).id;

	auto * pNode = AstNew(pParser, VarDeclStmt, makeStartEnd(iStart, iEnd));
	setIdent(&pNode->ident, pVarIdent, declScope);
	pNode->pInitExpr = pInitExpr;

    switch (parseTypeResult)
    {
        case PARSETYPERESULT_ParseSucceededTypeResolveSucceeded:
        {
		    Assert(isTypeResolved(typidResolved));
		    pNode->typid = typidResolved;
        } break;

        case PARSETYPERESULT_ParseSucceededTypeResolveFailed:
        {
        	pTypePending->pTypidUpdateWhenResolved = &pNode->typid;
        } break;

        case PARSETYPERESULT_ParseFailedButRecovered:
        {

        } break;

        default:
        {
            reportIceAndExit("Unknown PARSETYPERESULT %d", parseTypeResult);
            return nullptr;
        }
    }

	if (pNode->ident.pToken)
	{
		SymbolInfo varDeclInfo;
		setSymbolInfo(&varDeclInfo, pNode->ident, SYMBOLK_Var, Up(pNode));
		tryInsert(&pParser->symbTable, pNode->ident, varDeclInfo, pParser->scopeStack);
	}

	return Up(pNode);
}

AstNode * parseIfStmt(Parser * pParser)
{
    int iStart = peekTokenStartEnd(pParser->pScanner).iStart;

	// Parse 'if'

	if (!tryConsumeToken(pParser->pScanner, TOKENK_If))
	{
        auto startEndPrev = prevTokenStartEnd(pParser->pScanner);

		auto * pErr = AstNewErr0Child(pParser, ExpectedTokenkErr, makeStartEnd(startEndPrev.iEnd + 1));
		append(&pErr->aTokenkValid, TOKENK_If);
		return Up(pErr);
	}

	// Parse cond expr

	AstNode * pCondExpr = parseExpr(pParser);
	if (isErrorNode(*pCondExpr))
	{
		return pCondExpr;
	}

	// Parse 'do <stmt>' or block stmt

	AstNode * pThenStmt = parseDoStmtOrBlockStmt(pParser);
	if (isErrorNode(*pThenStmt))
	{
		auto * pErr = AstNewErr2Child(pParser, BubbleErr, gc_startEndBubble, pCondExpr, pThenStmt);
		return Up(pErr);
	}

	// Try parse 'else' statement

	AstNode * pElseStmt = nullptr;
	if (tryConsumeToken(pParser->pScanner, TOKENK_Else))
	{
		if (peekToken(pParser->pScanner) == TOKENK_If)
		{
			pElseStmt = parseIfStmt(pParser);
		}
		else
		{
			pElseStmt = parseDoStmtOrBlockStmt(pParser);
		}
	}

	if (pElseStmt && isErrorNode(*pElseStmt))
	{
		auto * pErr = AstNewErr3Child(pParser, BubbleErr, gc_startEndBubble, pCondExpr, pThenStmt, pElseStmt);
		return Up(pErr);
	}

	// Success!

    int iEnd = prevTokenStartEnd(pParser->pScanner).iEnd;

	auto * pNode = AstNew(pParser, IfStmt, makeStartEnd(iStart, iEnd));
	pNode->pCondExpr = pCondExpr;
	pNode->pThenStmt = pThenStmt;
	pNode->pElseStmt = pElseStmt;
	return Up(pNode);
}

AstNode * parseWhileStmt(Parser * pParser)
{
    int iStart = peekTokenStartEnd(pParser->pScanner).iStart;

	// Parse 'while'

	if (!tryConsumeToken(pParser->pScanner, TOKENK_While))
	{
        auto startEndPrev = prevTokenStartEnd(pParser->pScanner);

		auto * pErr = AstNewErr0Child(pParser, ExpectedTokenkErr, makeStartEnd(startEndPrev.iEnd));
		append(&pErr->aTokenkValid, TOKENK_While);
		return Up(pErr);
	}

	// Parse cond expr

	AstNode * pCondExpr = parseExpr(pParser);
	if (isErrorNode(*pCondExpr))
	{
		return pCondExpr;
	}

	// Parse 'do <stmt>' or block stmt

	AstNode * pBodyStmt = parseDoStmtOrBlockStmt(pParser);
	if (isErrorNode(*pBodyStmt))
	{
		auto * pErr = AstNewErr2Child(pParser, BubbleErr, gc_startEndBubble, pCondExpr, pBodyStmt);
		return Up(pErr);
	}

	// Success!

    int iEnd = prevTokenStartEnd(pParser->pScanner).iEnd;

	auto * pNode = AstNew(pParser, WhileStmt, makeStartEnd(iStart, iEnd));
	pNode->pCondExpr = pCondExpr;
	pNode->pBodyStmt = pBodyStmt;
	return Up(pNode);
}

AstNode * parseDoStmtOrBlockStmt(Parser * pParser, bool pushPopScopeBlock)
{
	TOKENK tokenk = peekToken(pParser->pScanner);
	if (tokenk != TOKENK_OpenBrace &&
		tokenk != TOKENK_Do)
	{
        auto startEndPrev = prevTokenStartEnd(pParser->pScanner);

		auto * pErr = AstNewErr0Child(pParser, ExpectedTokenkErr, makeStartEnd(startEndPrev.iEnd + 1));
		append(&pErr->aTokenkValid, TOKENK_OpenBrace);
		append(&pErr->aTokenkValid, TOKENK_Do);
		return Up(pErr);
	}

	AstNode * pStmt = nullptr;
	if (tokenk == TOKENK_OpenBrace)
	{
		pStmt = parseBlockStmt(pParser, pushPopScopeBlock);

		if (isErrorNode(*pStmt))
		{
			auto * pErr = AstNewErr1Child(pParser, BubbleErr, gc_startEndBubble, pStmt);
			return Up(pErr);
		}
	}
	else
	{
		Assert(tokenk == TOKENK_Do);
		consumeToken(pParser->pScanner);		// 'do'

		bool isDoStmt = true;
		pStmt = parseStmt(pParser, isDoStmt ? PARSESTMTK_DoStmt : PARSESTMTK_Stmt);

		Assert(pStmt->astk != ASTK_BlockStmt);
		Assert(pStmt->astk != ASTK_VarDeclStmt);

		if (isErrorNode(*pStmt))
		{
			auto * pErr = AstNewErr1Child(pParser, BubbleErr, gc_startEndBubble, pStmt);
			return Up(pErr);
		}
	}

	return pStmt;
}

AstNode * parseBlockStmt(Parser * pParser, bool pushPopScope)
{
    int iStart = peekTokenStartEnd(pParser->pScanner).iStart;
	// Parse {

	if (!tryConsumeToken(pParser->pScanner, TOKENK_OpenBrace))
	{
        auto startEndPrev = prevTokenStartEnd(pParser->pScanner);

		auto * pErr = AstNewErr0Child(pParser, ExpectedTokenkErr, makeStartEnd(startEndPrev.iEnd + 1));
		append(&pErr->aTokenkValid, TOKENK_OpenBrace);
		return Up(pErr);
	}

	if (pushPopScope) pushScope(pParser, SCOPEK_CodeBlock);
	Defer(if (pushPopScope) popScope(pParser));

	// Parse statements until }

	DynamicArray<AstNode *> apStmts;
	init(&apStmts);
	Defer(Assert(!apStmts.pBuffer));		// buffer should get "moved" into AST

	while (!tryConsumeToken(pParser->pScanner, TOKENK_CloseBrace))
	{
		AstNode * pStmt = parseStmt(pParser);
		append(&apStmts, pStmt);

		if (isErrorNode(*pStmt))
		{
			static const TOKENK s_aTokenkRecover[] = { TOKENK_CloseBrace, TOKENK_Semicolon };
			TOKENK tokenkRecover;
			if (tryRecoverFromPanic(pParser, s_aTokenkRecover, ArrayLen(s_aTokenkRecover), &tokenkRecover))
			{
				if (tokenkRecover == TOKENK_CloseBrace)
				{
					break;
				}
				else
				{
					Assert(tokenkRecover == TOKENK_Semicolon);
					continue;
				}
			}

			auto * pErr = AstNewErrListChildMove(pParser, BubbleErr, gc_startEndBubble, &apStmts);
			return Up(pErr);
		}
	}

	// Success!

    int iEnd = prevTokenStartEnd(pParser->pScanner).iEnd;

	auto * pNode = AstNew(pParser, BlockStmt, makeStartEnd(iStart, iEnd));
	initMove(&pNode->apStmts, &apStmts);
	pNode->scopeid = peekScope(pParser).id;

	return Up(pNode);
}

AstNode * parseReturnStmt(Parser * pParser)
{
    int iStart = peekTokenStartEnd(pParser->pScanner).iStart;

	// Parse 'return'

	if (!tryConsumeToken(pParser->pScanner, TOKENK_Return))
	{
        auto startEndPrev = prevTokenStartEnd(pParser->pScanner);

		auto * pErr = AstNewErr0Child(pParser, ExpectedTokenkErr, makeStartEnd(startEndPrev.iEnd + 1));
		append(&pErr->aTokenkValid, TOKENK_Return);
		return Up(pErr);
	}

	// Optionally parse expr

	AstNode * pExpr = nullptr;
	if (peekToken(pParser->pScanner) != TOKENK_Semicolon)
	{
		pExpr = parseExpr(pParser);

		if (isErrorNode(*pExpr))
		{
			return pExpr;
		}
	}

	// Parse ';'

	if (!tryConsumeToken(pParser->pScanner, TOKENK_Semicolon))
	{
        auto startEndPrev = prevTokenStartEnd(pParser->pScanner);

		AstExpectedTokenkErr * pErr;

		if (pExpr)
		{
			pErr = AstNewErr1Child(pParser, ExpectedTokenkErr, makeStartEnd(startEndPrev.iEnd + 1), pExpr);
		}
		else
		{
			pErr = AstNewErr0Child(pParser, ExpectedTokenkErr, makeStartEnd(startEndPrev.iEnd + 1));
		}

		append(&pErr->aTokenkValid, TOKENK_Semicolon);
		return Up(pErr);
	}

    int iEnd = prevTokenStartEnd(pParser->pScanner).iEnd;

	auto * pNode = AstNew(pParser, ReturnStmt, makeStartEnd(iStart, iEnd));
	pNode->pExpr = pExpr;
	return Up(pNode);
}

AstNode * parseBreakStmt(Parser * pParser)
{
    int iStart = peekTokenStartEnd(pParser->pScanner).iStart;

	if (!tryConsumeToken(pParser->pScanner, TOKENK_Break))
	{
        auto startEndPrev = prevTokenStartEnd(pParser->pScanner);

		auto * pErr = AstNewErr0Child(pParser, ExpectedTokenkErr, makeStartEnd(startEndPrev.iEnd + 1));
		append(&pErr->aTokenkValid, TOKENK_Break);
		return Up(pErr);
	}

	if (!tryConsumeToken(pParser->pScanner, TOKENK_Semicolon))
	{
        auto startEndPrev = prevTokenStartEnd(pParser->pScanner);

		auto * pErr = AstNewErr0Child(pParser, ExpectedTokenkErr, makeStartEnd(startEndPrev.iEnd + 1));
		append(&pErr->aTokenkValid, TOKENK_Semicolon);
		return Up(pErr);
	}

	// Success!

    int iEnd = prevTokenStartEnd(pParser->pScanner).iEnd;

	auto * pNode = AstNew(pParser, BreakStmt, makeStartEnd(iStart, iEnd));
	return Up(pNode);
}

AstNode * parseContinueStmt(Parser * pParser)
{
    int iStart = peekTokenStartEnd(pParser->pScanner).iStart;

	if (!tryConsumeToken(pParser->pScanner, TOKENK_Continue))
	{
        auto startEndPrev = prevTokenStartEnd(pParser->pScanner);

		auto * pErr = AstNewErr0Child(pParser, ExpectedTokenkErr, makeStartEnd(startEndPrev.iEnd + 1));
		append(&pErr->aTokenkValid, TOKENK_Continue);
		return Up(pErr);
	}

	if (!tryConsumeToken(pParser->pScanner, TOKENK_Semicolon))
	{
        auto startEndPrev = prevTokenStartEnd(pParser->pScanner);

		auto * pErr = AstNewErr0Child(pParser, ExpectedTokenkErr, makeStartEnd(startEndPrev.iEnd + 1));
		append(&pErr->aTokenkValid, TOKENK_Semicolon);
		return Up(pErr);
	}

	// Success!

    int iEnd = prevTokenStartEnd(pParser->pScanner).iEnd;

	auto * pNode = AstNew(pParser, ContinueStmt, makeStartEnd(iStart, iEnd));
	return Up(pNode);
}

AstNode * parseFuncDefnStmt(Parser * pParser)
{
	return parseFuncInternal(pParser, FUNCHEADERK_Defn);
}

AstNode * parseExpr(Parser * pParser)
{
	return parseBinop(pParser, s_aParseOp[s_iParseOpMax - 1]);
}

AstNode * parseBinop(Parser * pParser, const BinopInfo & op)
{
	auto oneStepLower = [](Parser * pParser, int precedence) -> AstNode *
	{
		Assert(precedence >= 0);

		if (precedence == 0)
		{
			return parseUnopPre(pParser);
		}
		else
		{
			const BinopInfo & opNext = s_aParseOp[precedence - 1];
			return parseBinop(pParser, opNext);
		}
	};

    int iStart = peekTokenStartEnd(pParser->pScanner).iStart;

	AstNode * pExpr = oneStepLower(pParser, op.precedence);
	Assert(pExpr);

	// NOTE: Error check is done after we get a chance to parse rhs so that we can catch errors in both expressions. If we don't
	//	match a binop then we just want to return pExpr as is anyway, whether it is an error or not.

	while (tryConsumeToken(pParser->pScanner, op.aTokenkMatch, op.cTokenMatch, ensurePendingToken(pParser)))
	{
		Token * pOp = claimPendingToken(pParser);

		AstNode * pRhsExpr = oneStepLower(pParser, op.precedence);
		Assert(pRhsExpr);

		if (isErrorNode(*pExpr) || isErrorNode(*pRhsExpr))
		{
			auto * pErr = AstNewErr2Child(pParser, BubbleErr, gc_startEndBubble, pExpr, pRhsExpr);
			return Up(pErr);
		}

        int iEnd = prevTokenStartEnd(pParser->pScanner).iEnd;

		auto * pNode = AstNew(pParser, BinopExpr, makeStartEnd(iStart, iEnd));
		pNode->pOp = pOp;
		pNode->pLhsExpr = pExpr;
		pNode->pRhsExpr = pRhsExpr;

		pExpr = Up(pNode);
	}

	return pExpr;
}

AstNode * parseUnopPre(Parser * pParser)
{
	if (tryConsumeToken(pParser->pScanner, g_aTokenkUnopPre, g_cTokenkUnopPre, ensurePendingToken(pParser)))
	{
        int iStart = prevTokenStartEnd(pParser->pScanner).iStart;

		Token * pOp = claimPendingToken(pParser);

		AstNode * pExpr = parseUnopPre(pParser);
		if (isErrorNode(*pExpr))
		{
			return pExpr;
		}

        int iEnd = prevTokenStartEnd(pParser->pScanner).iEnd;

		auto * pNode = AstNew(pParser, UnopExpr, makeStartEnd(iStart, iEnd));
		pNode->pOp = pOp;
		pNode->pExpr = pExpr;

		return Up(pNode);
	}
	else
	{
		return parsePrimary(pParser);
	}
}

AstNode * parsePrimary(Parser * pParser)
{
    int iStart = peekTokenStartEnd(pParser->pScanner).iStart;

	if (tryConsumeToken(pParser->pScanner, TOKENK_OpenParen, ensurePendingToken(pParser)))
	{
		// Group ( )

		Token * pOpenParen = claimPendingToken(pParser);
		AstNode * pExpr = parseExpr(pParser);

		if (isErrorNode(*pExpr))
		{
			return pExpr;
		}

		if (!tryConsumeToken(pParser->pScanner, TOKENK_CloseParen, ensurePendingToken(pParser)))
		{
            auto startEndPrev = prevTokenStartEnd(pParser->pScanner);

			auto * pErr = AstNewErr1Child(pParser, ExpectedTokenkErr, makeStartEnd(startEndPrev.iEnd + 1), pExpr);
			append(&pErr->aTokenkValid, TOKENK_CloseParen);
			return Up(pErr);
		}

        int iEnd = prevTokenStartEnd(pParser->pScanner).iEnd;

		auto * pNode = AstNew(pParser, GroupExpr, makeStartEnd(iStart, iEnd));
		pNode->pExpr = pExpr;

		return finishParsePrimary(pParser, Up(pNode));
	}
	else if (tryPeekToken(pParser->pScanner, g_aTokenkLiteral, g_cTokenkLiteral))
	{
		// Literal

		const static bool s_mustBeIntLiteral = false;
		return parseLiteralExpr(pParser, s_mustBeIntLiteral);
	}
	else if (peekToken(pParser->pScanner) == TOKENK_Identifier)
	{
		// Identifier

		AstNode * pVarOwner = nullptr;
		return parseVarExpr(pParser, pVarOwner);
	}
	else if (peekToken(pParser->pScanner) == TOKENK_Fn)
	{
		// Func literal

		AstNode * pNode = parseFuncLiteralExpr(pParser);

		if (isErrorNode(*pNode))
		{
			return pNode;
		}

		if (peekToken(pParser->pScanner) == TOKENK_OpenParen)
		{
			// HMM: I am torn about whether I want to call finishParsePrimaryHere,
			//	which would let you invoke a function literal at the spot that it is
			//	defined. Personally I am not convinced that that is good practice...
			//	It also doesn't work with the "do" syntax for one-liners.

			// NOTE: Current check for this is kind of a hack when really it would be
			//	better to make calling a func literal a valid parse, but a semantic error!
			//	Otherwise we would error on something like this since we are only checking
			//	for open parens...
			//	fn() -> () { doSomething(); }(;

            int iOpenParenStart = peekTokenStartEnd(pParser->pScanner).iStart;

			auto * pErr = AstNewErr1Child(pParser, InvokeFuncLiteralErr, makeStartEnd(iOpenParenStart), pNode);
			return Up(pErr);
		}

		return pNode;
	}
	else
	{
		return handleScanOrUnexpectedTokenkErr(pParser);
	}
}

AstNode * parseLiteralExpr(Parser * pParser, bool mustBeIntLiteralk)
{
	const static TOKENK s_tokenkIntLit = TOKENK_IntLiteral;

	const TOKENK * aTokenkValid = (mustBeIntLiteralk) ? &s_tokenkIntLit : g_aTokenkLiteral;
	const int cTokenkValid = (mustBeIntLiteralk) ? 1 : g_cTokenkLiteral;

	if (!tryConsumeToken(pParser->pScanner, aTokenkValid, cTokenkValid, ensurePendingToken(pParser)))
	{
        auto startEndPrev = prevTokenStartEnd(pParser->pScanner);

		AstExpectedTokenkErr * pErr = AstNewErr0Child(pParser, ExpectedTokenkErr, makeStartEnd(startEndPrev.iEnd + 1));
		appendMultiple(&pErr->aTokenkValid, aTokenkValid, cTokenkValid);

		return Up(pErr);
	}

	Token * pToken = claimPendingToken(pParser);

	auto * pExpr = AstNew(pParser, LiteralExpr, pToken->startEnd);
	pExpr->pToken = pToken;
	pExpr->literalk = literalkFromTokenk(pExpr->pToken->tokenk);
	pExpr->isValueSet = false;
	pExpr->isValueErroneous = false;

	return Up(pExpr);
}

AstNode * parseFuncLiteralExpr(Parser * pParser)
{
	return parseFuncInternal(pParser, FUNCHEADERK_Literal);
}

AstNode * parseVarExpr(Parser * pParser, AstNode * pOwnerExpr)
{
	if (!tryConsumeToken(pParser->pScanner, TOKENK_Identifier, ensurePendingToken(pParser)))
	{
        auto startEndPrev = prevTokenStartEnd(pParser->pScanner);

		AstExpectedTokenkErr * pErr;
		if (pOwnerExpr)
		{
			pErr = AstNewErr1Child(pParser, ExpectedTokenkErr, makeStartEnd(startEndPrev.iEnd + 1), pOwnerExpr);
		}
		else
		{
			pErr = AstNewErr0Child(pParser, ExpectedTokenkErr, makeStartEnd(startEndPrev.iEnd + 1));
		}

		append(&pErr->aTokenkValid, TOKENK_Identifier);
		return Up(pErr);
	}

	Token * pIdent = claimPendingToken(pParser);
	Assert(pIdent->tokenk == TOKENK_Identifier);

    // HMM: VarExpr start is really where its owner starts... is that what it *should* be?

    int iStart = pIdent->startEnd.iStart;
    if (pOwnerExpr)
    {
        iStart = getStartEnd(pParser->astDecs, pOwnerExpr->astid).iStart;
    }

	auto * pNode = AstNew(pParser, VarExpr, makeStartEnd(iStart, pIdent->startEnd.iEnd));
	pNode->pOwner = pOwnerExpr;
	pNode->pTokenIdent = pIdent;
	pNode->pResolvedDecl = nullptr;

	return finishParsePrimary(pParser, Up(pNode));
}

TypePendingResolution *
tryParseType(
	Parser * pParser,
	AstNode ** ppoErr)
{
	*ppoErr = nullptr;
	Type * pType = nullptr;

	if (tryConsumeToken(pParser->pScanner, TOKENK_OpenBracket))
	{
		// [

		// NOTE: No attempt is made to recover from errors inside of [ ] even though it is technically possible...
		//	it quickly becomes a quagmire as every recursive call to tryParseType needs to know/care if there
		//	was a recovered error. For now, if there is an error parsing a type you just call it quits and
		//	return an error.

		auto * pSubscriptExpr = parseExpr(pParser);

		if (isErrorNode(*pSubscriptExpr))
		{
			*ppoErr = pSubscriptExpr;
			return nullptr;
		}

		if (!tryConsumeToken(pParser->pScanner, TOKENK_CloseBracket))
		{
			auto startEndPrev = prevTokenStartEnd(pParser->pScanner);

			auto * pErr = AstNewErr1Child(pParser, ExpectedTokenkErr, makeStartEnd(startEndPrev.iEnd + 1), pSubscriptExpr);
			append(&pErr->aTokenkValid, TOKENK_CloseBracket);

			*ppoErr = pErr;
			return nullptr;
		}

		AstNode * pErrArray;
		TypePendingResolution * pTypePendingArray = tryParseType(pParser, &pErrArray);

		if (!pTypePendingArray)
		{
			Assert(pErrArray);
			*ppoErr = pErrArray;
			return nullptr;
		}
		else
		{
			// Success

			pType = newType(pParser);
			init(pType, TYPEK_Array);
			pType->pSubscriptExpr = pSubscriptExpr;
			pType->pTypeArray = pTypePendingArray->pType;
		}
	}
	else if (tryConsumeToken(pParser->pScanner, TOKENK_Carat))
	{
		// ^

		AstNode * pErrPtr;
		TypePendingResolution * pTypePendingPtr = tryParseType(pParser, &pErrPtr);

		if (!pTypePendingPtr)
		{
			Assert(pErrPtr);
			*ppoErr = pErrPtr;
			return nullptr;
		}
		else
		{
			// Success

			pType = newType(pParser);
			init(pType, TYPEK_Pointer);
			pType->pTypePtr = pTypePendingPtr->pType;
		}
	}
	else if (tryConsumeToken(pParser->pScanner, TOKENK_Identifier, ensurePendingToken(pParser)))
	{
		// ident (success)

		pType = newType(pParser);
		init(pType, TYPEK_Base);

		Token * pTypeIdent = claimPendingToken(pParser);
		Assert(pTypeIdent->tokenk == TOKENK_Identifier);
		setIdentNoScope(&pType->ident, pTypeIdent);
	}
	else if (tryConsumeToken(pParser->pScanner, TOKENK_Fn))
	{
		// fn

		// TODO...
	}
	else
	{
		AstNode * pErr = handleScanOrUnexpectedTokenkErr(pParser, nullptr);
		*ppoErr = pErr;
		return nullptr;
	}

	Assert(pType);
	Assert(!*ppoErr);

	TypePendingResolution * pTypePending = appendNew(&pParser->typeTable.typesPendingResolution);
	pTypePending->pType = pType;
	pTypePending->pTypidUpdateWhenResolved = nullptr;	// NOTE: Caller is responsible for setting this (if they care to)

	return pTypePending;

	// if (*poHadErrorButRecovered)
	// {
	// 	// Parse through but don't actually try to resolve a type

	// 	TOKENK tokenkPeek = peekToken(pParser->pScanner);
	// 	if (tokenkPeek == TOKENK_Identifier)
	// 	{
	// 		consumeToken(pParser->pScanner);
	// 		return false;
	// 	}
	// 	else if (tokenkPeek == TOKENK_Fn)
	// 	{
    //         FuncType dummy;
    //         init(&dummy);
    //         Defer(dispose(&dummy));

	// 		AstErr * pErr = nullptr;
	// 		bool parseHeaderSuccess = tryParseFuncHeaderTypeOnly(pParser, &dummy, &pErr);

	// 		Assert(Implies(parseHeaderSuccess, !pErr));
	// 		Assert(Implies(!parseHeaderSuccess, pErr));

	// 		if (!parseHeaderSuccess)
	// 		{
	// 			appendMultiple(&UpErr(pErr)->apChildren, apNodeChildren);
	// 		}

	// 		*ppoErr = pErr;
	// 		return parseHeaderSuccess;
	// 	}
	// 	else
	// 	{
	// 		AstNode * pErr = handleScanOrUnexpectedTokenkErr(pParser, nullptr);
	// 		appendMultiple(&UpErr(pErr)->apChildren, apNodeChildren);

	// 		*ppoErr = pErr;
	// 		return false;
	// 	}
	// }
    // else
    // {
    //     // So far so good...

    //     TOKENK tokenkPeek = peekToken(pParser->pScanner);
    //     if (tokenkPeek == TOKENK_Identifier)
    //     {
    //         // Non-func type

    //         Type * pType = newType(pParser);
    //         init(pType, false /* isFuncType */);
    //         reinitMove(&pType->aTypemods, &aModifiers);

    //         Token * pTypeIdent = ensureAndClaimPendingToken(pParser);
    //         consumeToken(pParser->pScanner, pTypeIdent);
    //         Assert(pTypeIdent->tokenk == TOKENK_Identifier);

    //         setIdentNoScope(&pType->ident, pTypeIdent);

    //         // TYPID typid = resolveIntoTypeTableOrSetPending(pParser, pType, ppoTypePendingResolution);
    //         // if (isTypeResolved(typid))
    //         // {
    //         //     *poTypidResolved = typid;
    //         //     return PARSETYPERESULT_ParseSucceededTypeResolveSucceeded;
    //         // }
    //         // else
    //         // {
    //         //     return PARSETYPERESULT_ParseSucceededTypeResolveFailed;
    //         // }

	// 		TypePendingResolution * pTypePending = appendNew(&pParser->typeTable.typesPendingResolution);
	// 		pTypePending->pType = pType;
	// 		pTypePending->pTypidUpdateWhenResolved = nullptr;	// NOTE: Caller is responsible for setting this

	// 		*ppoTypePending = pTypePending;
	// 		return true;
    //     }
    //     else if (tokenkPeek == TOKENK_Fn)
    //     {
    //         // Func type

    //         bool parseHeaderResult;

    //         Type * pType = newType(pParser);
    //         init(pType, true /* isFuncType */);
    //         reinitMove(&pType->aTypemods, &aModifiers);
    //         Defer(
    //             if (!parseHeaderResult)
    //             {
    //                 dispose(pType);
    //                 releaseType(pParser, pType);
    //             }
    //         );

	// 		bool hadErrorButRecovered = false;

    //         AstErr * pErr = nullptr;
    //         parseHeaderResult = tryParseFuncHeaderTypeOnly(pParser, &pType->funcType, &pErr, &hadErrorButRecovered);

    //         Assert(Implies(parseHeaderResult, !pErr));
	// 		Assert(Implies(!parseHeaderResult, pErr));

	// 		*poHadErrorButRecovered = *poHadErrorButRecovered || hadErrorButRecovered;

	// 		if (!parseHeaderResult)
	// 		{
	// 			appendMultiple(&UpErr(pErr)->apChildren, apNodeChildren);
	// 			*ppoErr = pErr;
	// 			return false;
	// 		}
	// 		else
	// 		{
	// 			TypePendingResolution * pTypePending = appendNew(&pParser->typeTable.typesPendingResolution);
	// 			pTypePending->pType = pType;
	// 			pTypePending->pTypidUpdateWhenResolved = nullptr;	// NOTE: Caller is responsible for setting this

	// 			*ppoTypePending = pTypePending;
	// 			return true;
	// 		}
    //     }
    //     else
    //     {
    //         AstNode * pErr = handleScanOrUnexpectedTokenkErr(pParser, nullptr);
	// 		appendMultiple(&UpErr(pErr)->apChildren, apNodeChildren);
	// 		*ppoErr = pErr;
    //         return false;
    //     }
    // }
}

bool
tryParseFuncHeaderTypeOnly(
    Parser * pParser,
    FuncType * poFuncType,
    AstErr ** ppoErr,
	bool * poHadErrorButRecovered)
{
	// NOTE: poFuncType should be inited by caller

	*poHadErrorButRecovered = false;

	// Parse "fn"

	if (!tryConsumeToken(pParser->pScanner, TOKENK_Fn))
	{
        auto startEndPrev = prevTokenStartEnd(pParser->pScanner);

		auto * pErr = AstNewErr0Child(pParser, ExpectedTokenkErr, makeStartEnd(startEndPrev.iEnd + 1));
		append(&pErr->aTokenkValid, TOKENK_Fn);

		*ppoErr = UpErr(pErr);
		return false;
	}

	// Array subscript expressions are AstNodes that we have to attach as children if we error

	DynamicArray<AstNode *> apNodeChildren;
	init(&apNodeChildren);
	Defer(dispose(&apNodeChildren));

	auto tryParseParameterTypes = [](
		Parser * pParser,
		PARAMK paramk,
		DynamicArray<TYPID> * paTypids,				// Value we are computing. We will set the typid's of the types we can resolve and set the others to pending
		DynamicArray<AstNode *> * papNodeChildren,	// Boookkeeping so caller can attach children to errors
		bool * poHadErrorButRecovered)
		-> bool
	{
		*poHadErrorButRecovered = false;

		// Parse (

		bool singleReturnWithoutParens = false;
		if (!tryConsumeToken(pParser->pScanner, TOKENK_OpenParen))
		{
			if (paramk != PARAMK_Return)
			{
                auto startEndPrev = prevTokenStartEnd(pParser->pScanner);

				auto * pErr = AstNewErr0Child(pParser, ExpectedTokenkErr, makeStartEnd(startEndPrev.iEnd + 1));
				append(&pErr->aTokenkValid, TOKENK_OpenParen);
				append(papNodeChildren, Up(pErr));
				return false;
			}
		}

		bool isFirstParam = true;
		while (true)
		{
			if (!singleReturnWithoutParens && tryConsumeToken(pParser->pScanner, TOKENK_CloseParen))
				break;

			if (singleReturnWithoutParens && !isFirstParam)
				break;

			// ,

			if (!isFirstParam && !tryConsumeToken(pParser->pScanner, TOKENK_Comma))
			{
                auto startEndPrev = prevTokenStartEnd(pParser->pScanner);

				auto * pErr = AstNewErr0Child(pParser, ExpectedTokenkErr, makeStartEnd(startEndPrev.iEnd + 1));
				append(&pErr->aTokenkValid, TOKENK_Comma);
				append(papNodeChildren, Up(pErr));
				return false;
			}

			// type

			{
				bool hadErrorButRecovered;
				AstNode * pErr;
				TypePendingResolution * pTypePending;

				bool success = tryParseType(pParser, &pTypePending, &pErr, &hadErrorButRecovered);
				*poHadErrorButRecovered = *poHadErrorButRecovered || hadErrorButRecovered;

				if (!success)
				{
					Assert(pErr);
					append(papNodeChildren, Up(pErr));
					return false;
				}
				else
				{
					if (hadErrorButRecovered)
					{
					}
					else
					{
						// TODO: Pending typid
					}
				}
				if (parseTypeResult == PARSETYPERESULT_ParseSucceededTypeResolveSucceeded)
				{
					Assert(isTypeResolved(typidResolved));
					append(paTypids, typidResolved);
				}
				else if (parseTypeResult == PARSETYPERESULT_ParseSucceededTypeResolveFailed)
				{
					Assert(pTypePending);

					TYPID * pTypidPending = appendNew(paTypids);
					*pTypidPending = TYPID_Unresolved;

					pTypePending->pTypidUpdateWhenResolved = pTypidPending;
				}
                else
                {
                    // Assert(parseTypeResult == PARSETYPERESULT_ParseFailedButRecovered);
                    // append(paTypids, TYPID_Unresolved);
                    // recoveredFromPanic = true;
                }
			}

			// name (optional)
			// HMM: It feels funny to allow an identifier but to not put it in the symbol table or bind it to anything.
			//	but the documentation benefits just feel so good... if this is something I run into in other scenarios,
			//	maybe introduce throwaway, unbound identifiers as a concept in the language with its own syntax.
			//	Maybe something like fn (int #startIndex, int #count) ... but that would rule out # for things like
			//	compiler directives!
			// Maybe define // to not comment to the end of the line if it isn't followed by a space? So it would
			//	look like fn (int //startIndex, int //count)... I actually don't hate how that looks, even though
			//	the semantics are kinda unusual.


			tryConsumeToken(pParser->pScanner, TOKENK_Identifier);

			isFirstParam = false;
		}

		return
            recoveredFromPanic ?
            PARSEFUNCHEADERTYPEONLYRESULT_FailedButRecovered :
            PARSEFUNCHEADERTYPEONLYRESULT_Succeeded;
	};

	//
	// Parse in parameters
	//
    {
		bool hadErrorButRecovered;
        bool success = tryParseParameterTypes(pParser, PARAMK_Param, &poFuncType->paramTypids, &apNodeChildren, &hadErrorButRecovered);
        if (!success)
        {
            auto * pErr = AstNewErrListChildMove(pParser, BubbleErr, gc_startEndBubble, &apNodeChildren);
            *ppoErr = UpErr(pErr);
            return false;
        }

		*poHadErrorButRecovered = hadErrorButRecovered;
    }

	// Parse ->

	bool arrowOmitted = false;
	if (!tryConsumeToken(pParser->pScanner, TOKENK_MinusGreater))
	{
		arrowOmitted = true;
	}

	//
	// Parse out parameters
	//

	if (!arrowOmitted)
	{
		bool hadErrorButRecovered;
        bool success = tryParseParameterTypes(pParser, PARAMK_Return, &poFuncType->returnTypids, &apNodeChildren, &hadErrorButRecovered);
		if (!success)
		{
			auto * pErr = AstNewErrListChildMove(pParser, BubbleErr, gc_startEndBubble, &apNodeChildren);
			*ppoErr = UpErr(pErr);
			return false;
		}

		*poHadErrorButRecovered = *poHadErrorButRecovered || hadErrorButRecovered;
	}

	return true;
}

AstNode * finishParsePrimary(Parser * pParser, AstNode * pExpr)
{
	// Parse post-fix operations and allow them to chain

    int iStart = getStartEnd(pParser->astDecs, pExpr->astid).iStart;

	if (tryConsumeToken(pParser->pScanner, TOKENK_Dot, ensurePendingToken(pParser)))
	{
		// Member access

		return parseVarExpr(pParser, pExpr);
	}
	else if (tryConsumeToken(pParser->pScanner, TOKENK_Carat, ensurePendingToken(pParser)))
	{
		// Pointer dereference

        int iEnd = prevTokenStartEnd(pParser->pScanner).iEnd;

		auto * pNode = AstNew(pParser, PointerDereferenceExpr, makeStartEnd(iStart, iEnd));
		pNode->pPointerExpr = pExpr;

		return finishParsePrimary(pParser, Up(pNode));
	}
	else if (tryConsumeToken(pParser->pScanner, TOKENK_OpenBracket, ensurePendingToken(pParser)))
	{
		// Array access

		static const bool s_mustBeIntLiteral = true;		// TODO: Support arbitrary expressions (semantic pass makes sure they are compile time const)
		AstNode * pSubscriptExpr = parseLiteralExpr(pParser, s_mustBeIntLiteral);
		if (isErrorNode(*pSubscriptExpr))
		{
			if (tryRecoverFromPanic(pParser, TOKENK_CloseBracket))
			{
				// Embed error in pSubscriptExpr

				goto arrayEnd;
			}
			else
			{
				// Couldn't recover

				auto * pErr = AstNewErr2Child(pParser, BubbleErr, gc_startEndBubble, pExpr, pSubscriptExpr);
				return Up(pErr);
			}
		}
		else if (!tryConsumeToken(pParser->pScanner, TOKENK_CloseBracket, ensurePendingToken(pParser)))
		{
			// Missing ]

            auto startEndPrev = prevTokenStartEnd(pParser->pScanner);

			auto * pErr = AstNewErr2Child(pParser, ExpectedTokenkErr, makeStartEnd(startEndPrev.iEnd + 1), pExpr, pSubscriptExpr);
			append(&pErr->aTokenkValid, TOKENK_CloseBracket);

			if (tryRecoverFromPanic(pParser, TOKENK_CloseBracket))
			{
				// Embed error in pSubscriptExpr

				append(&UpErr(pErr)->apChildren, pSubscriptExpr);
				pSubscriptExpr = Up(pErr);
				goto arrayEnd;
			}
			else
			{
				// Couldn't recover

				return Up(pErr);
			}
		}

	arrayEnd:
        int iEnd = prevTokenStartEnd(pParser->pScanner).iEnd;

		auto * pNode = AstNew(pParser, ArrayAccessExpr, makeStartEnd(iStart, iEnd));
		pNode->pArrayExpr = pExpr;
		pNode->pSubscriptExpr = pSubscriptExpr;

		return finishParsePrimary(pParser, Up(pNode));
	}
	else if (tryConsumeToken(pParser->pScanner, TOKENK_OpenParen, ensurePendingToken(pParser)))
	{
		// Func call

		DynamicArray<AstNode *> apArgs;
		init(&apArgs);
		Defer(Assert(!apArgs.pBuffer));		// buffer should get "moved" into AST

		bool isFirstArg = true;
		bool isCommaAlreadyAccountedFor = false;

		static const TOKENK s_aTokenkRecover[] = { TOKENK_Comma, TOKENK_CloseParen };

		while (!tryConsumeToken(pParser->pScanner, TOKENK_CloseParen, ensurePendingToken(pParser)))
		{
			if (!isFirstArg && !isCommaAlreadyAccountedFor && !tryConsumeToken(pParser->pScanner, TOKENK_Comma))
			{
				// Missing ,

                auto startEndPrev = prevTokenStartEnd(pParser->pScanner);

				auto * pErr = AstNewErr0Child(pParser, ExpectedTokenkErr, makeStartEnd(startEndPrev.iEnd + 1));
				append(&pErr->aTokenkValid, TOKENK_Comma);

				TOKENK tokenkRecover;
				if (tryRecoverFromPanic(pParser, s_aTokenkRecover, ArrayLen(s_aTokenkRecover), &tokenkRecover))
				{
					append(&apArgs, Up(pErr));	// Error node simply slots into an argument

					if (tokenkRecover == TOKENK_Comma)
					{
						// Do nothing (this substitutes as the comma we were missing)
					}
					else
					{
						Assert(tokenkRecover == TOKENK_CloseParen);
						break;;
					}
				}
				else
				{
					// Couldn't recover

					prepend(&apArgs, pExpr);
					reinitMove(&DownErr(pErr)->apChildren, &apArgs);
					return Up(pErr);
				}
			}

			isCommaAlreadyAccountedFor = false;		// For next iteration...

			// Parse expr

			AstNode * pExprArg = parseExpr(pParser);

			if (isErrorNode(*pExprArg))
			{
				// Erroneous expr

				TOKENK tokenkRecover;
				if (tryRecoverFromPanic(pParser, s_aTokenkRecover, ArrayLen(s_aTokenkRecover), &tokenkRecover))
				{
					// Slot error into arg if we recovered

					append(&apArgs, pExprArg);

					if (tokenkRecover == TOKENK_Comma)
					{
						isCommaAlreadyAccountedFor = true;
						continue;
					}
					else
					{
						Assert(tokenkRecover == TOKENK_CloseParen);
						break;;
					}
				}
				else
				{
					// Couldn't recover

					prepend(&apArgs, pExpr);
					auto * pErrNode = AstNewErrListChildMove(pParser, BubbleErr, gc_startEndBubble, &apArgs);
					return Up(pErrNode);
				}
			}
			else
			{
				// Add expr to args

				append(&apArgs, pExprArg);
			}

			isFirstArg = false;
		}

        int iEnd = prevTokenStartEnd(pParser->pScanner).iEnd;

		auto * pNode = AstNew(pParser, FuncCallExpr, makeStartEnd(iStart, iEnd));
		pNode->pFunc = pExpr;
		initMove(&pNode->apArgs, &apArgs);

		return finishParsePrimary(pParser, Up(pNode));
	}
	else
	{
		return pExpr;
	}
}

AstNode * parseFuncHeaderGrp(Parser * pParser, FUNCHEADERK funcheaderk, bool * poHadErrorButRecovered)
{
    Assert(poHadErrorButRecovered);
    *poHadErrorButRecovered = false;

	Assert(funcheaderk == FUNCHEADERK_Defn || funcheaderk == FUNCHEADERK_Literal);

	bool isDefn = (funcheaderk == FUNCHEADERK_Defn);
    ScopedIdentifier identDefn;     // Only valid if isDefn

	bool success = false;
	Token * pTokIdent = nullptr;
	Defer(
		if (!success && isDefn) release(&pParser->tokenAlloc, pTokIdent)
	);

    int iStart = peekTokenStartEnd(pParser->pScanner).iStart;

	// Parse "fn"

	if (!tryConsumeToken(pParser->pScanner, TOKENK_Fn))
	{
        auto startEndPrev = prevTokenStartEnd(pParser->pScanner);

		success = false;
		auto * pErr = AstNewErr0Child(pParser, ExpectedTokenkErr, makeStartEnd(startEndPrev.iEnd + 1));
		append(&pErr->aTokenkValid, TOKENK_Fn);
		return Up(pErr);
	}

	// Parse definition name

	if (isDefn)
	{
		if (!tryConsumeToken(pParser->pScanner, TOKENK_Identifier, ensurePendingToken(pParser)))
		{
            auto startEndPrev = prevTokenStartEnd(pParser->pScanner);

			success = false;
            auto * pErr = AstNewErr0Child(pParser, ExpectedTokenkErr, makeStartEnd(startEndPrev.iEnd + 1));
			append(&pErr->aTokenkValid, TOKENK_Identifier);
			return Up(pErr);
		}

		pTokIdent = claimPendingToken(pParser);

		// NOTE: Use prev scopeid for the func defn ident instead of the one that the func itself
		//	has pushed!

		setIdent(&identDefn, pTokIdent, peekScopePrev(pParser).id);
	}
	else
	{
		if (tryConsumeToken(pParser->pScanner, TOKENK_Identifier, ensurePendingToken(pParser)))
		{
			// TODO: Maybe this should be a more specific/informative error, since we basically
			//	understand what they are trying to do. Something like NamedFuncNonDefnErr...?
			//	This is probably a good test case for adding more "context" to errors!!!

			Token * pErrToken = claimPendingToken(pParser);
			Assert(pErrToken->tokenk == TOKENK_Identifier);

			success = false;
			auto * pErr = AstNewErr0Child(pParser, UnexpectedTokenkErr, pErrToken->startEnd);
			pErr->pErrToken = pErrToken;
			return Up(pErr);
		}
	}

	// Parse "in" parameters

    bool paramRecoveredError = false;
    bool returnRecoveredError = false;

	AstNode * pParamListGrp = parseParamOrReturnListGrp(pParser, PARAMK_Param, &paramRecoveredError);
	if (isErrorNode(*pParamListGrp))
	{
		success = false;
		auto * pErr = AstNewErr0Child(pParser, BubbleErr, gc_startEndBubble);
		return Up(pErr);
	}

	// Parse ->

	bool arrowOmitted = false;
	if (!tryConsumeToken(pParser->pScanner, TOKENK_MinusGreater))
	{
		arrowOmitted = true;
	}

	//
	// Parse out parameters if there was a ->
	//

	AstNode * pReturnListGrp = nullptr;
	if (arrowOmitted)
	{
        auto startEndPrev = prevTokenStartEnd(pParser->pScanner);

		auto * pReturnGrpEmpty = AstNew(pParser, ReturnListGrp, makeStartEnd(startEndPrev.iEnd + 1));
		init(&pReturnGrpEmpty->apVarDecls);
		pReturnListGrp = Up(pReturnGrpEmpty);
	}
	else
	{
		pReturnListGrp = parseParamOrReturnListGrp(pParser, PARAMK_Return, &returnRecoveredError);
		if (isErrorNode(*pReturnListGrp))
		{
			success = false;
			auto * pErr = AstNewErr1Child(pParser, BubbleErr, gc_startEndBubble, pParamListGrp);
			return Up(pErr);
		}
	}
	Assert(pReturnListGrp);

    *poHadErrorButRecovered = paramRecoveredError || returnRecoveredError;

	// Success!

	success = true;
    int iEnd = prevTokenStartEnd(pParser->pScanner).iEnd;

	if (isDefn)
	{
        // NOTE: parseFuncDefnStmt is responsible for inserting into the symbol table which will poke in our symbseqid

		auto * pNode = AstNew(pParser, FuncDefnHeaderGrp, makeStartEnd(iStart, iEnd));
		pNode->ident = identDefn;
        pNode->symbseqid = SYMBSEQID_Unset;
		pNode->pParamListGrp = pParamListGrp;
		pNode->pReturnListGrp = pReturnListGrp;

		return Up(pNode);
	}
	else
	{
		auto * pNode = AstNew(pParser, FuncLiteralHeaderGrp, makeStartEnd(iStart, iEnd));
		pNode->pParamListGrp = pParamListGrp;
		pNode->pReturnListGrp = pReturnListGrp;

		return Up(pNode);
	}
}

AstNode * parseParamOrReturnListGrp(Parser * pParser, PARAMK paramk, bool * poHadErrorButRecovered)
{
	Assert(poHadErrorButRecovered);
	*poHadErrorButRecovered = false;

    int iStart = peekTokenStartEnd(pParser->pScanner).iStart;

	bool singleReturnWithoutParens = false;
	if (!tryConsumeToken(pParser->pScanner, TOKENK_OpenParen))
	{
		if (paramk != PARAMK_Return)
		{
            auto startEndPrev = prevTokenStartEnd(pParser->pScanner);

			auto * pErr = AstNewErr0Child(pParser, ExpectedTokenkErr, makeStartEnd(startEndPrev.iEnd + 1));
			append(&pErr->aTokenkValid, TOKENK_OpenParen);
			return Up(pErr);
		}
		else
		{
			singleReturnWithoutParens = true;
		}
	}

	static const TOKENK s_aTokenkRecoverWithParen[] = { TOKENK_Comma, TOKENK_CloseParen };
	static const TOKENK s_aTokenkRecoverWithoutParen[] = { TOKENK_Comma };

	bool isFirstParam = true;

	DynamicArray<AstNode *> apVarDecls;
	init(&apVarDecls);
	Defer(Assert(!apVarDecls.pBuffer));

	while (true)
	{
		if (!singleReturnWithoutParens && tryConsumeToken(pParser->pScanner, TOKENK_CloseParen))
			break;

		if (singleReturnWithoutParens && !isFirstParam)
			break;

		if (!isFirstParam && !tryConsumeToken(pParser->pScanner, TOKENK_Comma))
		{
			// Missing ,

            auto startEndPrev = prevTokenStartEnd(pParser->pScanner);

			TOKENK tokenkRecover;
			if (tryRecoverFromPanic(
					pParser,
					(singleReturnWithoutParens) ? s_aTokenkRecoverWithoutParen : s_aTokenkRecoverWithParen,
					(singleReturnWithoutParens) ? ArrayLen(s_aTokenkRecoverWithoutParen) : ArrayLen(s_aTokenkRecoverWithParen),
					&tokenkRecover))
			{
				*poHadErrorButRecovered = true;

				if (tokenkRecover == TOKENK_Comma)
				{
					// Do nothing (this substitutes as the comma we were missing)
				}
				else
				{
					Assert(tokenkRecover == TOKENK_CloseParen);
					break;;
				}
			}
			else
			{
				// Couldn't recover

				auto * pErr = AstNewErrListChildMove(pParser, ExpectedTokenkErr, makeStartEnd(startEndPrev.iEnd + 1), &apVarDecls);
				append(&pErr->aTokenkValid, TOKENK_Comma);
				return Up(pErr);
			}
		}

		const static EXPECTK s_expectkVarName = EXPECTK_Optional;
		const static EXPECTK s_expectkSemicolon = EXPECTK_Forbidden;

		AstNode * pNode = parseVarDeclStmt(pParser, s_expectkVarName, s_expectkSemicolon);
		append(&apVarDecls, pNode);

		if (isErrorNode(*pNode))
		{
			// Erroneous vardecl

			TOKENK tokenkRecover;
			if (tryRecoverFromPanic(
					pParser,
					(singleReturnWithoutParens) ? s_aTokenkRecoverWithoutParen : s_aTokenkRecoverWithParen,
					(singleReturnWithoutParens) ? ArrayLen(s_aTokenkRecoverWithoutParen) : ArrayLen(s_aTokenkRecoverWithParen),
					&tokenkRecover))
			{
				*poHadErrorButRecovered = true;

				if (tokenkRecover == TOKENK_Comma)
				{
					continue;
				}
				else
				{
					Assert(tokenkRecover == TOKENK_CloseParen);
					break;;
				}
			}

			// Couldn't recover

			auto * pErr = AstNewErrListChildMove(pParser, BubbleErr, gc_startEndBubble, &apVarDecls);
			return Up(pErr);
		}

		isFirstParam = false;
	}

	// Success!

    int iEnd = prevTokenStartEnd(pParser->pScanner).iEnd;

	if (paramk == PARAMK_Param)
	{
		auto * pNode = AstNew(pParser, ParamListGrp, makeStartEnd(iStart, iEnd));
		initMove(&pNode->apVarDecls, &apVarDecls);
		return Up(pNode);
	}
	else
	{
		Assert(paramk == PARAMK_Return);

		auto * pNode = AstNew(pParser, ReturnListGrp, makeStartEnd(iStart, iEnd));
		initMove(&pNode->apVarDecls, &apVarDecls);
		return Up(pNode);
	}
}

AstNode * parseFuncInternal(Parser * pParser, FUNCHEADERK funcheaderk)
{
	Assert(funcheaderk == FUNCHEADERK_Defn || funcheaderk == FUNCHEADERK_Literal);

	bool success = false;
	bool isDefn = (funcheaderk == FUNCHEADERK_Defn);

	int iStart = peekTokenStartEnd(pParser->pScanner).iStart;

	// Parse header

	// NOTE: Push scope before parsing header so that the symbols declared in the header
	//	are subsumed by the function's scope.

	pushScope(pParser, SCOPEK_CodeBlock);
	Defer(if (!success) popScope(pParser));		// NOTE: In success case we pop it manually before inserting into symbol table

	//ScopedIdentifier identDefn;		// Only valid if isDefn
	//DynamicArray<AstNode *> apParamVarDecl;
	//DynamicArray<AstNode *> apReturnVarDecl;
	//init(&apParamVarDecl);
	//init(&apReturnVarDecl);

    bool hadErrorButRecovered;
    AstNode * pFuncHeaderGrp = parseFuncHeaderGrp(pParser, funcheaderk, &hadErrorButRecovered);
    if (isErrorNode(*pFuncHeaderGrp))
    {
        success = false;
        auto * pErr = AstNewErr0Child(pParser, BubbleErr, gc_startEndBubble);
        return Up(pErr);
    }

	// Parse { <stmts> } or do <stmt>

	bool pushPopScope = false;
	AstNode * pBody = parseDoStmtOrBlockStmt(pParser, pushPopScope);

	if (isErrorNode(*pBody))
	{
        success = false;
		auto * pErr = AstNewErr1Child(pParser, BubbleErr, gc_startEndBubble, pFuncHeaderGrp);
		return Up(pErr);
	}

	success = true;
	int iEnd = prevTokenStartEnd(pParser->pScanner).iEnd;

	if (!isDefn)
	{
		auto * pNode = AstNew(pParser, FuncLiteralExpr, makeStartEnd(iStart, iEnd));
        pNode->pFuncLiteralHeaderGrp = pFuncHeaderGrp;
		pNode->pBodyStmt = pBody;
		pNode->scopeid = peekScope(pParser).id;

		return Up(pNode);
	}
	else
	{
		auto * pNode = AstNew(pParser, FuncDefnStmt, makeStartEnd(iStart, iEnd));
        pNode->pFuncDefnHeaderGrp = pFuncHeaderGrp;
		pNode->pBodyStmt = pBody;
		pNode->scopeid = peekScope(pParser).id;


		popScope(pParser);

        if (!hadErrorButRecovered)
        {
		    // Insert into symbol table (pop first so the scope stack doesn't include the scope that the function itself pushed)
            // TODO: Even if we had an error but recovered, we *know* the symbol name -- so we should probably insert some sort of
            //  special value to prevent unresolved symbol errors.

            ScopedIdentifier ident = Down(pFuncHeaderGrp, FuncDefnHeaderGrp)->ident;

            SymbolInfo funcDefnInfo;
            setSymbolInfo(&funcDefnInfo, ident, SYMBOLK_Func, Up(pNode));
            tryInsert(&pParser->symbTable, ident, funcDefnInfo, pParser->scopeStack);
        }

		return Up(pNode);
	}
}

AstNode * handleScanOrUnexpectedTokenkErr(Parser * pParser, DynamicArray<AstNode *> * papChildren)
{
	Token * pErrToken =	 ensureAndClaimPendingToken(pParser);
	consumeToken(pParser->pScanner, pErrToken);

	if (pErrToken->tokenk == TOKENK_Error)
	{
		// Scan err

		if (papChildren)
		{
			auto * pErr = AstNewErrListChildMove(pParser, ScanErr, pErrToken->startEnd, papChildren);
			pErr->pErrToken = pErrToken;
			return Up(pErr);
		}
		else
		{
			auto * pErr = AstNewErr0Child(pParser, ScanErr, pErrToken->startEnd);
			pErr->pErrToken = pErrToken;
			return Up(pErr);
		}
	}
	else
	{
		// Unexpected tokenk err

		if (papChildren)
		{
			auto * pErr = AstNewErrListChildMove(pParser, UnexpectedTokenkErr, pErrToken->startEnd, papChildren);
			pErr->pErrToken = pErrToken;
			return Up(pErr);
		}
		else
		{
			auto * pErr = AstNewErr0Child(pParser, UnexpectedTokenkErr, pErrToken->startEnd);
			pErr->pErrToken = pErrToken;
			return Up(pErr);
		}
	}
}

void pushScope(Parser * pParser, SCOPEK scopek)
{
	Scope s;
	s.scopek = scopek;

	if (scopek == SCOPEK_BuiltIn)
	{
		for (int i = 0; i < pParser->scopeStack.a.cItem; i++)
		{
			AssertInfo(pParser->scopeStack.a[i].id != SCOPEID_BuiltIn, "Shouldn't contain built-in scope id twice!");
		}

		s.id = SCOPEID_BuiltIn;
	}
	else if (scopek == SCOPEK_Global)
	{
		for (int i = 0; i < pParser->scopeStack.a.cItem; i++)
		{
			AssertInfo(pParser->scopeStack.a[i].id != SCOPEID_Global, "Shouldn't contain global scope id twice!");
		}

		s.id = SCOPEID_Global;
	}
	else
	{
		s.id = pParser->scopeidNext;
		pParser->scopeidNext = static_cast<SCOPEID>(pParser->scopeidNext + 1);
	}


	push(&pParser->scopeStack, s);
}

Scope peekScope(Parser * pParser)
{
	bool success;
	Scope s = peek(pParser->scopeStack, &success);
	Assert(success);
	return s;
}

Scope peekScopePrev(Parser * pParser)
{
	bool success;
	Scope s = peekFar(pParser->scopeStack, 1, &success);
	Assert(success);
	return s;
}

Scope popScope(Parser * pParser)
{
	bool success;
	Scope s = pop(&pParser->scopeStack, &success);
	Assert(success);

	return s;
}

void reportScanAndParseErrors(const Parser & parser)
{
	for (int i = 0; i < parser.apErrorNodes.cItem; i++)
	{
		auto * pNode = parser.apErrorNodes[i];
		Assert(category(pNode->astk) == ASTCATK_Error);
		Assert(pNode->astk != ASTK_BubbleErr);

		switch (pNode->astk)
		{
			case ASTK_ScanErr:
			{
				auto * pErr = Down(pNode, ScanErr);
				Assert(pErr->pErrToken->tokenk == TOKENK_Error);

				DynamicArray<String> aStrScanError;
				init(&aStrScanError);
				Defer(dispose(&aStrScanError));

                errMessagesFromGrferrtok(pErr->pErrToken->grferrtok, &aStrScanError);

				Assert(aStrScanError.cItem > 0);
				for (int i = 0; i < aStrScanError.cItem; i++)
				{
					reportScanError(*parser.pScanner, *pErr->pErrToken, aStrScanError[i].pBuffer);
				}
			}
			break;

			case ASTK_BubbleErr:
			{
				AssertInfo(false, "Bubble errors should not be included in the error node list!");
			}
			break;

			case ASTK_UnexpectedTokenkErr:
			{
                auto * pErr = Down(pNode, UnexpectedTokenkErr);
                reportParseError(parser, *pNode, "Unexpected %s", g_mpTokenkDisplay[pErr->pErrToken->tokenk]);
			}
			break;

			case ASTK_ExpectedTokenkErr:
			{
				auto * pErr = Down(pNode, ExpectedTokenkErr);

				Assert(pErr->aTokenkValid.cItem > 0);

				if (pErr->aTokenkValid.cItem == 1)
				{
                    reportParseError(parser, *pNode, "Expected %s", g_mpTokenkDisplay[pErr->aTokenkValid[0]]);
				}
				else if (pErr->aTokenkValid.cItem == 2)
				{
                    reportParseError(parser, *pNode, "Expected %s or %s", g_mpTokenkDisplay[pErr->aTokenkValid[0]], g_mpTokenkDisplay[pErr->aTokenkValid[1]]);
				}
				else
				{
                    String strError;
                    init(&strError, "Expected ");
                    append(&strError, g_mpTokenkDisplay[pErr->aTokenkValid[0]]);

					for (uint i = 1; i < pErr->aTokenkValid.cItem; i++)
					{
                        append(&strError, ", ");

						bool isLast = (i == pErr->aTokenkValid.cItem - 1);
						if (isLast) append(&strError, "or ");


                        append(&strError, g_mpTokenkDisplay[pErr->aTokenkValid[i]]);
					}

                    reportParseError(parser, *pNode, strError.pBuffer);
				}
			}
			break;

			case ASTK_InitUnnamedVarErr:
			{
                reportParseError(parser, *pNode, "Cannot initialize unnamed variable");
			}
			break;

			case ASTK_ChainedAssignErr:
			{
                reportParseError(parser, *pNode, "Chaining assignments is not permitted");
			}
			break;

			case ASTK_IllegalDoStmtErr:
			{
				auto * pErr = Down(pNode, IllegalDoStmtErr);
                reportParseError(parser, *pNode, "%s is not permitted following 'do'", displayString(pErr->astkStmt, true /* capitalize */));
			}
			break;

			case ASTK_IllegalTopLevelStmtErr:
			{
				auto * pErr = Down(pNode, IllegalTopLevelStmtErr);
                reportParseError(parser, *pNode, "%s is not permitted as a top level statement", displayString(pErr->astkStmt, true /* capitalize */));
			}
			break;

			case ASTK_InvokeFuncLiteralErr:
			{
                reportParseError(parser, *pNode, "Function literal can not be directly invoked");
			}
			break;

			default:
			{
				reportIceAndExit("Unknown parse error: %d", pNode->astk);
			}
			break;
		}
	}
}

AstNode * astNew(Parser * pParser, ASTK astk, StartEndIndices startEnd)
{
	AstNode * pNode = allocate(&pParser->astAlloc);
	pNode->astk = astk;
	pNode->astid = static_cast<ASTID>(pParser->iNode);
	pParser->iNode++;

    decorate(&pParser->astDecs.startEndDecoration, pNode->astid, startEnd);

	return pNode;
}

AstNode * astNewErr(Parser * pParser, ASTK astkErr, StartEndIndices startEnd, AstNode * pChild0, AstNode * pChild1, AstNode * pChild2)
{
	Assert(Implies(pChild2, pChild1));
	Assert(Implies(pChild1, pChild0));

	AstNode * apChildren[3] = { pChild0, pChild1, pChild2 };
	int cPChildren = (pChild2) ? 3 : (pChild1) ? 2 : (pChild0) ? 1 : 0;
	return astNewErr(pParser, astkErr, startEnd, apChildren, cPChildren);
}

AstNode * astNewErr(Parser * pParser, ASTK astkErr, StartEndIndices startEnd, AstNode * apChildren[], uint cPChildren)
{
	Assert(category(astkErr) == ASTCATK_Error);

	auto * pNode = DownErr(astNew(pParser, astkErr, startEnd));
	init(&pNode->apChildren);
	appendMultiple(&pNode->apChildren, apChildren, cPChildren);

#if DEBUG
	bool hasErrorChild = false;
	if (astkErr == ASTK_BubbleErr)
	{
		for (int i = 0; i < pNode->apChildren.cItem; i++)
		{
			if (isErrorNode(*(pNode->apChildren[i])))
			{
				hasErrorChild = true;
				break;
			}
		}

		if (!hasErrorChild)
		{
			AssertInfo(false, "Bubble child by definition should have 1 or more error child(ren)");
		}
	}
#endif

	if (astkErr != ASTK_BubbleErr)
	{
		append(&pParser->apErrorNodes, Up(pNode));
	}

	return Up(pNode);
}

AstNode * astNewErrMoveChildren(Parser * pParser, ASTK astkErr, StartEndIndices startEnd, DynamicArray<AstNode *> * papChildren)
{
	Assert(category(astkErr) == ASTCATK_Error);

	auto * pNode = DownErr(astNew(pParser, astkErr, startEnd));
	initMove(&pNode->apChildren, papChildren);

#if DEBUG
	bool hasErrorChild = false;
	if (astkErr == ASTK_BubbleErr)
	{
		for (int i = 0; i < pNode->apChildren.cItem; i++)
		{
			if (isErrorNode(*(pNode->apChildren[i])))
			{
				hasErrorChild = true;
				break;
			}
		}

		if (!hasErrorChild)
		{
			AssertInfo(false, "Bubble child by definition should have 1 or more error child(ren)");
		}
	}
#endif

	if (astkErr != ASTK_BubbleErr)
	{
		append(&pParser->apErrorNodes, Up(pNode));
	}

	return Up(pNode);
}

bool tryRecoverFromPanic(Parser * pParser, TOKENK tokenkRecover)
{
	return tryRecoverFromPanic(pParser, &tokenkRecover, 1);
}

bool tryRecoverFromPanic(Parser * pParser, const TOKENK * aTokenkRecover, int cTokenkRecover, TOKENK * poTokenkMatch)
{
    // Consume tokens until we hit a recoverable token or a semicolon IN OUR CURRENT CONTEXT. New contexts are pushed
    //  when we consume (, {, or [. We need to pop any pushed context before we can match a recoverable token.

	// NOTE: Matching a semicolon returns true if it is one of our "recoverable" tokens, and
	//	false otherwise.

	// NOTE: The token that we successfully match to recover also gets consumed.

#if DEBUG
	for (int i = 0; i < cTokenkRecover; i++)
	{
		TOKENK tokenkRecover = aTokenkRecover[i];

		AssertInfo(
			tokenkRecover == TOKENK_Semicolon ||
			tokenkRecover == TOKENK_Comma ||
			tokenkRecover == TOKENK_CloseParen ||
			tokenkRecover == TOKENK_CloseBrace ||
			tokenkRecover == TOKENK_CloseBracket,
			"Only these tokens are supported right now"
		);
	}
#endif

	// NOTE: This will consume EOF token if it hits it!

	if (poTokenkMatch) *poTokenkMatch = TOKENK_Nil;

    int cParensPushed = 0;
	int cBracesPushed = 0;
	int cBracketsPushed = 0;

	while(true)
	{
		if (isFinished(pParser->pScanner)) return false;

		TOKENK tokenk = peekToken(pParser->pScanner);

		// Push contexts

		switch (tokenk)
		{
			case TOKENK_OpenParen:		cParensPushed++; break;
			case TOKENK_OpenBrace:		cBracesPushed++; break;
			case TOKENK_OpenBracket:	cBracketsPushed++; break;
		}

		bool isAnyContextPushed =
			cParensPushed != 0 ||
			cBracesPushed != 0 ||
			cBracketsPushed != 0;

		if (isAnyContextPushed)
		{
			// Pop contexts

			switch (tokenk)
			{
				case TOKENK_CloseParen:		cParensPushed = Max(cParensPushed - 1, 0); break;
				case TOKENK_CloseBrace:		cBracesPushed = Max(cBracesPushed - 1, 0); break;
				case TOKENK_CloseBracket:	cBracketsPushed = Max(cBracketsPushed - 1, 0); break;
			}
		}
		else
		{
			// Try to match!

			for (int i = 0; i < cTokenkRecover; i++)
			{
				TOKENK tokenkRecover = aTokenkRecover[i];

				if (tokenkRecover == tokenk)
				{
					if (poTokenkMatch) *poTokenkMatch = tokenk;
					consumeToken(pParser->pScanner);
					return true;
				}
			}

			if (tokenk == TOKENK_Semicolon)
			{
				consumeToken(pParser->pScanner);
				return false;
			}
		}

		// Keep chewing!

		consumeToken(pParser->pScanner);
	}
}

Token * ensurePendingToken(Parser * pParser)
{
	if (!pParser->pPendingToken)
	{
		pParser->pPendingToken = allocate(&pParser->tokenAlloc);
	}

	return pParser->pPendingToken;
}

Token * claimPendingToken(Parser * pParser)
{
	Assert(pParser->pPendingToken);

	Token * pResult = pParser->pPendingToken;
	pParser->pPendingToken = nullptr;
	return pResult;
}

Token * ensureAndClaimPendingToken(Parser * pParser)
{
	ensurePendingToken(pParser);
	return claimPendingToken(pParser);
}
