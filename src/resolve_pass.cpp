#include "resolve_pass.h"

#include "ast.h"
#include "error.h"
#include "symbol.h"
#include "type.h"

scopeid resolveVarExpr(ResolvePass * pPass, AstNode * pVarExpr)
{
	Assert(pVarExpr);

	if (pVarExpr->astk != ASTK_VarExpr)
	{
		AssertInfo(false, "TODO: This is not correct. We need to be able to apply a member selection operator to ANY expression");
		return gc_unresolvedScopeid;
	}

	auto * pExpr = Down(pVarExpr, VarExpr);
	if (pExpr->pOwner)
	{
		// TODO: This is too simple. Owner can be more than a VarExr
		scopeid ownerScopeid = resolveVarExpr(pPass, pExpr->pOwner);

		AssertInfo(!pExpr->pResolvedDecl, "This shouldn't be resolved yet because this is the code that should resolve it!");

		ScopedIdentifier candidate;
        setIdent(&candidate, pExpr->pTokenIdent, ownerScopeid);
		SymbolInfo * pSymbInfo = lookupVarSymb(*pPass->pSymbTable, candidate);

		if (pSymbInfo)
		{
			// Resolve!

            pExpr->pResolvedDecl = pSymbInfo->pVarDeclStmt;

			// TODO: Look up type of the variable and return the type declaration's corresponding scopeid
		}
		else
		{
			// Unresolved

			return gc_unresolvedScopeid;
		}
	}
	else
	{

	}

	// do lookup + resolve and then return scopeid?

    AssertInfo(false, "TODO: finish writing this function");
    return gc_unresolvedScopeid;
}

// TODO: Should probably return a type. Maybe I want to assign all types a typeid and register them in a table instead
//	of storing Type's directly in the AST? Kind of a flyweight pattern? I also know I want typeid stuff for RTTI so might
//	as well do that now...

scopeid resolveExpr(ResolvePass * pPass, AstNode * pNode)
{
	Assert(category(pNode->astk) == ASTCATK_Expr);

	switch (pNode->astk)
	{
        case ASTK_BinopExpr:
        {
            auto * pExpr = DownConst(pNode, BinopExpr);
            doResolvePass(pPass, pExpr->pLhsExpr);
            doResolvePass(pPass, pExpr->pRhsExpr);
        } break;

        case ASTK_GroupExpr:
        {
            auto * pExpr = DownConst(pNode, GroupExpr);
            doResolvePass(pPass, pExpr->pExpr);
        } break;

        case ASTK_LiteralExpr:
        {
        } break;

        case ASTK_UnopExpr:
        {
            auto * pExpr = DownConst(pNode, UnopExpr);
            doResolvePass(pPass, pExpr->pExpr);
        } break;

        case ASTK_VarExpr:
        {
            auto * pExpr = DownConst(pNode, VarExpr);

			// TODO: Need to look up variables in the owner struct's in the structs namespace....
			//	we should probably recurse into the owner first, and maybe it will return the symbolid of the
			//	scope that the owner's shit lives in?

        } break;

        case ASTK_ArrayAccessExpr:
        {
            auto * pExpr = DownConst(pNode, ArrayAccessExpr);
            doResolvePass(pPass, pExpr->pArray);
            doResolvePass(pPass, pExpr->pSubscript);
        } break;

        case ASTK_FuncCallExpr:
        {
            auto * pExpr = DownConst(pNode, FuncCallExpr);
            doResolvePass(pPass, pExpr->pFunc);

            for (int i = 0; i < pExpr->apArgs.cItem; i++)
            {
                doResolvePass(pPass, pExpr->apArgs[i]);
            }
        } break;

        case ASTK_FuncLiteralExpr:
        {
            auto * pExpr = DownConst(pNode, FuncLiteralExpr);

			push(&pPass->scopeidStack, pExpr->scopeid);
			Defer(pop(&pPass->scopeidStack));

			for (int i = 0; i < pExpr->apParamVarDecls.cItem; i++)
			{
				doResolvePass(pPass, pExpr->apParamVarDecls[i]);
			}

			for (int i = 0; i < pExpr->apReturnVarDecls.cItem; i++)
			{
				doResolvePass(pPass, pExpr->apReturnVarDecls[i]);
			}
        } break;

		default:
		{
			reportIceAndExit("Unknown astk in resolveExpr: %d", pNode->astk);
		} break;
	}

    // TODO: return scopeid
    // should I also return typid? Or should I embed typid's into the ast nodes even for exprs... probably...
}

void resolveStmt(ResolvePass * pPass, AstNode * pNode)
{
	Assert(category(pNode->astk) == ASTCATK_Expr);

	switch (pNode->astk)
	{
		case ASTK_ExprStmt:
		{
			auto * pStmt = DownConst(pNode, ExprStmt);
			doResolvePass(pPass, pStmt->pExpr);
		} break;

		case ASTK_AssignStmt:
		{
			auto * pStmt = DownConst(pNode, AssignStmt);
			doResolvePass(pPass, pStmt->pLhsExpr);
			doResolvePass(pPass, pStmt->pRhsExpr);
		} break;

		case ASTK_VarDeclStmt:
		{
			auto * pStmt = DownConst(pNode, VarDeclStmt);
			AssertInfo(isScopeSet(pStmt->ident), "The name of the thing you are declaring should be resolved to itself...");

			AssertInfo(isTypeResolved(pStmt->typid), "Inferred types are TODO");
            const Type * pType = lookupType(*pPass->pTypeTable, pStmt->typid);

			if (pType->isFuncType)
			{
				// Resolve types of params

                // I think this is unnecessarry? This is just a type declaration, so the parameters/return values have optional,
                //  unbound names. The only names that matter are the type identifiers which should already be resolved.

				/*for (uint i = 0; i < pType->funcType.apParamVarDecls.cItem; i++)
				{
					doResolvePass(pPass, pType->pFuncType->apParamVarDecls[i]);
				}

				for (uint i = 0; i < pStmt->pType->pFuncType->apReturnVarDecls.cItem; i++)
				{
					doResolvePass(pPass, pStmt->pType->pFuncType->apReturnVarDecls[i]);
				}*/
			}
			else
			{
                // DITTO for above comment....

				//// Resolve type

				//AssertInfo(!isScopeSet(pStmt->pType->ident), "This shouldn't be resolved yet because this is the code that should resolve it!");

				//ScopedIdentifier candidate = pStmt->pType->ident;

				//for (uint i = 0; i < count(pPass->scopeidStack); i++)
				//{
				//	scopeid scopeid;
				//	Verify(peekFar(pPass->scopeidStack, i, &scopeid));
				//	resolveIdentScope(&candidate, scopeid);

				//	SymbolInfo * pSymbInfo = lookupStruct(pPass->pSymbTable, candidate);

				//	if(pSymbInfo)
				//	{
				//		// Resolve it!

				//		resolveIdentScope(&pStmt->pType->ident, scopeid);
				//		break;
				//	}
				//}

				//if (!isScopeSet(pStmt->pType->ident))
				//{
				//	reportUnresolvedIdentError(pPass, pStmt->pType->ident);
				//}
			}

			// Record symbseqid for variable

			if (pStmt->ident.pToken)
			{
#if DEBUG
				SymbolInfo * pSymbInfo = lookupVarSymb(*pPass->pSymbTable, pStmt->ident);
				AssertInfo(pSymbInfo, "We should have put this var decl in the symbol table when we parsed it...");
				Assert(pSymbInfo->symbolk == SYMBOLK_Var);
				AssertInfo(pSymbInfo->pVarDeclStmt == pStmt, "At var declaration we should be able to look up the var in the symbol table and find ourselves...");
#endif
				pPass->lastSymbseqid = pStmt->symbseqid;
			}

            // Resolve init expr

            if (pStmt->pInitExpr)
            {
                doResolvePass(pPass, pStmt->pInitExpr);
            }
		} break;

		case ASTK_StructDefnStmt:
		{
			auto * pStmt = DownConst(pNode, StructDefnStmt);

#if DEBUG
			// Verify assumptions

			SymbolInfo * pSymbInfo = lookupTypeSymb(*pPass->pSymbTable, pStmt->ident);
			AssertInfo(pSymbInfo, "We should have put this struct decl in the symbol table when we parsed it...");
			Assert(pSymbInfo->symbolk == SYMBOLK_Struct);
			AssertInfo(pSymbInfo->pStructDefnStmt == pStmt, "At struct definition we should be able to look up the struct in the symbol table and find ourselves...");
#endif

			// Record sequence id

			Assert(pStmt->symbseqid != gc_unsetSymbseqid);
			pPass->lastSymbseqid = pStmt->symbseqid;

			// Record scope id

			push(&pPass->scopeidStack, pStmt->scopeid);
			Defer(pop(&pPass->scopeidStack));

			// Resolve member decls

			for (int i = 0; i < pStmt->apVarDeclStmt.cItem; i++)
			{
				doResolvePass(pPass, pStmt->apVarDeclStmt[i]);
			}
		} break;

		case ASTK_FuncDefnStmt:
		{
			auto * pStmt = DownConst(pNode, FuncDefnStmt);

#if DEBUG
            {
                // Verify assumptions

                DynamicArray<SymbolInfo> * paSymbInfo;
                paSymbInfo = lookupFuncSymb(*pPass->pSymbTable, pStmt->ident);

                AssertInfo(paSymbInfo && paSymbInfo->cItem > 0, "We should have put this func decl in the symbol table when we parsed it...");
                bool found = false;

                for (int i = 0; i < paSymbInfo->cItem; i++)
                {
                    SymbolInfo * pSymbInfo = &(*paSymbInfo)[i];
                    Assert(pSymbInfo->symbolk == SYMBOLK_Func);

                    if (pStmt->typid == pSymbInfo->pFuncDefnStmt->typid)
                    {
                        found = true;
                        break;
                    }
                }

                AssertInfo(found, "At struct definition we should be able to look up the struct in the symbol table and find ourselves...");
            }
#endif

			// Record sequence id

			Assert(pStmt->symbseqid != gc_unsetSymbseqid);
			pPass->lastSymbseqid = pStmt->symbseqid;

			// Record scope id

			push(&pPass->scopeidStack, pStmt->scopeid);
			Defer(pop(&pPass->scopeidStack));

			// Resolve params

			for (int i = 0; i < pStmt->apParamVarDecls.cItem; i++)
			{
				doResolvePass(pPass, pStmt->apParamVarDecls[i]);
			}

			// Resolve return values

			for (int i = 0; i < pStmt->apReturnVarDecls.cItem; i++)
			{
				doResolvePass(pPass, pStmt->apReturnVarDecls[i]);
			}

			// Resolve body

			doResolvePass(pPass, pStmt->pBodyStmt);
		} break;

		case ASTK_BlockStmt:
		{
			// NOTE: For functions, block statements aren't responsible for pushing a new scope, since
			//	the block scope should be the same as the scope of the params. That's why we only
			//	push/pop if we are actually a different scope!

			auto * pStmt = DownConst(pNode, BlockStmt);

			bool shouldPushPop = true;
			{
				scopeid scopeidPrev;
				if (peek(pPass->scopeidStack, &scopeidPrev))
				{
					shouldPushPop = (scopeidPrev != pStmt->scopeid);
				}
			}

			if (shouldPushPop) push(&pPass->scopeidStack, pStmt->scopeid);
			Defer(if (shouldPushPop) pop(&pPass->scopeidStack));

			for (int i = 0; i < pStmt->apStmts.cItem; i++)
			{
				doResolvePass(pPass, pStmt->apStmts[i]);
			}
		} break;

		case ASTK_WhileStmt:
		{
			auto * pStmt = DownConst(pNode, WhileStmt);
			doResolvePass(pPass, pStmt->pCondExpr);
			doResolvePass(pPass, pStmt->pBodyStmt);
		} break;

		case ASTK_IfStmt:
		{
			auto * pStmt = DownConst(pNode, IfStmt);
			doResolvePass(pPass, pStmt->pCondExpr);
			doResolvePass(pPass, pStmt->pThenStmt);
			if (pStmt->pElseStmt) doResolvePass(pPass, pStmt->pElseStmt);
		} break;

		case ASTK_ReturnStmt:
		{
			auto * pStmt = DownConst(pNode, ReturnStmt);
			doResolvePass(pPass, pStmt->pExpr);
		} break;

		case ASTK_BreakStmt:
		{
			// Nothing
		} break;

		case ASTK_ContinueStmt:
		{
			// Nothing
		} break;

	    default:
	    {
		    reportIceAndExit("Unknown astk in resolveStmt: %d", pNode->astk);
	    } break;
	}
}

void doResolvePass(ResolvePass * pPass, AstNode * pNode) // TODO: rename this param to pNode
{
	AssertInfo(
		pNode,
		"For optional node children, it is the caller's responsibility to perform null check, because I want to catch "
		"errors if we ever accidentally have null children in spots where they shouldn't be null"
	);

    switch (category(pNode->astk))
    {
		case ASTCATK_Expr:
		{
			resolveExpr(pPass, pNode);
		} break;

		case ASTCATK_Stmt:
		{
			resolveStmt(pPass, pNode);
		} break;

        // PROGRAM

        case ASTK_Program:
        {
            auto * pProgram = DownConst(pNode, Program);
            for (int i = 0; i < pProgram->apNodes.cItem; i++)
            {
                doResolvePass(pPass, pProgram->apNodes[i]);
            }
        } break;

        default:
		{
			reportIceAndExit("Unknown astcatk in doResolvePass: %d", category(pNode->astk));
		} break;
    }
}

void reportUnresolvedIdentError(ResolvePass * pPass, ScopedIdentifier ident)
{
	// TODO: Insert into some sort of error proxy table. But I would need all lookups to also check this table!

	// SymbolInfo info;
	// setSymbolInfo(&info, ident, SYMBOLK_ErrorProxy, nullptr);
	// Verify(tryInsert(pPass->pSymbolTable, ident, info));
}
