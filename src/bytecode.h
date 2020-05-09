#pragma once

#include "als.h"
#include "ast.h"

struct MeekCtx;

// ByteCode OPeration

enum BCOP : u8
{
	BCOP_Return,

	// Load Immediate
	//	- Pushes 1 n-bit value from bytecode to stack

	BCOP_LoadImmediate8,
	BCOP_LoadImmediate16,
	BCOP_LoadImmediate32,
	BCOP_LoadImmediate64,
	BCOP_LoadTrue,
	BCOP_LoadFalse,

	// Load
	//	- Pops address from stack
	//	- Dereferences it
	//	- Pushes dereferenced n-bit value to the stack

	// HMM: Version of load address that reads the uintptr from bytecode? I suspect I will have lots of
	//	LoadImmediate followed by LoadAddress as a way to read values out of variables...

	BCOP_Load8,
	BCOP_Load16,
	BCOP_Load32,
	BCOP_Load64,

	// Store
	//	- Pops n-bit value from stack
	//	- Pops address from stack
	//	- Stores value at address

	BCOP_Store8,
	BCOP_Store16,
	BCOP_Store32,
	BCOP_Store64,

	// Duplicate
	//	- Peeks n-bit value at top of stack
	//	- Pushes a copy of it

	BCOP_Duplicate8,
	BCOP_Duplicate16,
	BCOP_Duplicate32,
	BCOP_Duplicate64,

	// Add Int
	//	- Pops 2 n-bit int values from stack
	//	- Adds them
	//	- Pushes n-bit int result onto stack

	BCOP_AddInt8,
	BCOP_AddInt16,
	BCOP_AddInt32,
	BCOP_AddInt64,

	// Sub Int
	//	- Pops n-bit int subtrahend from the stack
	//	- Pops n-bit int minuend from the stack
	//	- Subtracts them (minuend - subtrahend)
	//	- Pushes n-bit int result onto stack

	BCOP_SubInt8,
	BCOP_SubInt16,
	BCOP_SubInt32,
	BCOP_SubInt64,

	// Mul Int
	//	- Pops 2 n-bit int values from stack
	//	- Multiplies them
	//	- Pushes n-bit int result onto stack

	BCOP_MulInt8,
	BCOP_MulInt16,
	BCOP_MulInt32,
	BCOP_MulInt64,

	// Div Int
	//	- Pops n-bit int divisor from the stack
	//	- Pops n-bit int dividend from the stack
	//	- Divides them (dividend / divisor) ; Integer division is performed
	//	- Pushes n-bit int result onto stack
	//	* Use S instructions for signed division, and U instructions for unsigned

	BCOP_DivS8,
	BCOP_DivS16,
	BCOP_DivS32,
	BCOP_DivS64,

	BCOP_DivU8,
	BCOP_DivU16,
	BCOP_DivU32,
	BCOP_DivU64,

	// Add Float
	//	- Pops 2 n-bit float values from stack
	//	- Adds them
	//	- Pushes n-bit float result onto stack

	BCOP_AddFloat32,
	BCOP_AddFloat64,

	// Sub Float
	//	- Pops n-bit float subtrahend from the stack
	//	- Pops n-bit float minuend from the stack
	//	- Subtracts them (minuend - subtrahend)
	//	- Pushes n-bit float result onto stack

	BCOP_SubFloat32,
	BCOP_SubFloat64,

	// Mul Float
	//	- Pops 2 n-bit float values from stack
	//	- Multiplies them
	//	- Pushes n-bit float result onto stack

	BCOP_MulFloat32,
	BCOP_MulFloat64,

	// Div Float
	//	- Pops n-bit float divisor from the stack
	//	- Pops n-bit float dividend from the stack
	//	- Divides them (dividend / divisor)
	//	- Pushes n-bit float result onto stack

	BCOP_DivFloat32,
	BCOP_DivFloat64,

	// Negate Signed Int
	//	- Pops n-bit int off the stack
	//	- Pushes its negated value onto the stack

	BCOP_NegateS8,
	BCOP_NegateS16,
	BCOP_NegateS32,
	BCOP_NegateS64,

	// Negate Float
	//	- Pops n-bit float off the stack
	//	- Pushes its negated value onto the stack

	BCOP_NegateFloat32,
	BCOP_NegateFloat64,

	// Jump
	//	- Reads s16 from bytecode
	//	- Advances IP that many bytes

	BCOP_Jump,

	// Jump
	//	- Reads 8 bit bool off the stack
	//	- Reads s16 from bytecode
	//	- Advances IP that many bytes if the bool is false

	BCOP_JumpIfFalse,

	// StackAlloc
	//	- Reads uintptr from bytecode
	//	- Reserves that many bytes on the top of the stack

	BCOP_StackAlloc,

	// StackFree
	//	- Reads uintptr from bytecode
	//	- Pops that many bytes off the top of the stack

	BCOP_StackFree,

	// Debug Print (debug only, subject to removal!)
	//	- Reads typid from bytecode
	//	- Pops n-bit (n determined by typid) value off the stack
	//	- Prints output according to typid + value

	BCOP_DebugPrint,

	// Exit the program

	BCOP_DebugExit,

	BCOP_Max,
	BCOP_Nil = static_cast<u8>(0xFF),

#ifdef _WIN64
	BCOP_LoadImmediatePtr = BCOP_LoadImmediate64,
#elif WIN32
	BCOP_LoadImmediatePtr = BCOP_LoadImmediate32,
#else
	StaticAssertNotCompiled;
#endif
};

// extern const int gc_mpBcopCByte[];

enum SIZEDBCOP
{
	SIZEDBCOP_LoadImmediate,
	SIZEDBCOP_Load,
	SIZEDBCOP_Store,
	SIZEDBCOP_Duplicate,
	SIZEDBCOP_AddInt,
	SIZEDBCOP_SubInt,
	SIZEDBCOP_MulInt,
	SIZEDBCOP_DivSignedInt,
	SIZEDBCOP_DivUnsignedInt,
	SIZEDBCOP_AddFloat,
	SIZEDBCOP_SubFloat,
	SIZEDBCOP_MulFloat,
	SIZEDBCOP_DivFloat,
	SIZEDBCOP_NegateSigned,
	SIZEDBCOP_NegateFloat,

	SIZEDBCOP_Nil = -1
};
BCOP bcopSized(SIZEDBCOP sizedBcop, int cBit);


struct BytecodeFunction
{
	DynamicArray<u8> bytes;
	DynamicArray<int> sourceLineNumbers;
};

void init(BytecodeFunction * pBcf);
void dispose(BytecodeFunction * pBcf);

struct BytecodeBuilder
{
	MeekCtx * pCtx;

	DynamicArray<BytecodeFunction> aBytecodeFunc;
	BytecodeFunction * pBytecodeFuncMain;

	// Per-func compilation

	BytecodeFunction * pBytecodeFuncCompiling;
	bool root = false;

	struct NodeCtx
	{
		AstNode * pNode;
		bool wantsChildExprAddr;

		// Tag implied by pNode->astk

		union
		{
			struct UIfStmtCtx
			{
				int iJumpArgPlaceholder;	// Byte index of jump arg that needs to be backpatched
				int ipZero;					// Resulting IP if we were to jump with argument of 0
			} ifStmtData;
		};
	};

	Stack<NodeCtx> nodeCtxStack;
};

void init(BytecodeBuilder * pBuilder, MeekCtx * pCtx);
void dispose(BytecodeBuilder * pBuilder);

void init(BytecodeBuilder::NodeCtx * pNodeCtx, AstNode * pNode);

void compileBytecode(BytecodeBuilder * pBuilder);
void emitOp(BytecodeFunction * bcf, BCOP byteEmit, int lineNumber);
void emit(BytecodeFunction * bcf, u8 byteEmit);
void emit(BytecodeFunction * bcf, s8 byteEmit);
void emit(BytecodeFunction * bcf, u16 bytesEmit);
void emit(BytecodeFunction * bcf, s16 bytesEmit);
void emit(BytecodeFunction * bcf, u32 bytesEmit);
void emit(BytecodeFunction * bcf, s32 bytesEmit);
void emit(BytecodeFunction * bcf, u64 bytesEmit);
void emit(BytecodeFunction * bcf, s64 bytesEmit);
void emit(BytecodeFunction * bcf, f32 bytesEmit);
void emit(BytecodeFunction * bcf, f64 bytesEmit);
void emit(BytecodeFunction * bcf, void * pBytesEmit, int cBytesEmit);

void backpatch(BytecodeFunction * bcf, int iByte, s16 bytesUpdate);
void backpatch(BytecodeFunction * bcf, int iByte, void * pBytesUpdate, int cBytesUpdate);

bool visitBytecodeBuilderPreorder(AstNode * pNode, void * pBuilder_);
void visitBytecodeBuilderPostOrder(AstNode * pNode, void * pBuilder_);
void visitBytecodeBuilderHook(AstNode * pNode, AWHK awhk, void * pBuilder_);

#ifdef DEBUG
void disassemble(const BytecodeFunction & bcf);
#endif
