#include "LLVM/LLVMcodegen.h"

#define genError(...) errorMessage("LLVM codegen: " __VA_ARGS__)

// Definitions

LLVMCodeGenerator *createLLVMCodeGenerator(Vector *sourceFiles) {
	LLVMCodeGenerator *self = safeMalloc(sizeof(*self));
	self->abstractSyntaxTree = NULL;
	self->currentNode = 0;
	self->sourceFiles = sourceFiles;
	self->builder = LLVMCreateBuilder();
	self->namedValues = hashmap_new();
	return self;
}

void destroyLLVMCodeGenerator(LLVMCodeGenerator *self) {
	for (int i = 0; i < self->sourceFiles->size; i++) {
		SourceFile *sourceFile = getVectorItem(self->sourceFiles, i);
		LLVMDisposeModule(sourceFile->module);
		destroySourceFile(sourceFile);
		verboseModeMessage("Destroyed source files iteration %d", i);
	}

	// TODO clear named values
	hashmap_free(self->namedValues);

	free(self);
	verboseModeMessage("Destroyed compiler");
}

static void consumeAstNode(LLVMCodeGenerator *self) {
	self->currentNode += 1;
}

static void consumeAstNodeBy(LLVMCodeGenerator *self, int amount) {
	self->currentNode += amount;
}

bool isFloatingType(LLVMValueRef type) {
	switch (LLVMGetTypeKind(type)) {
		case LLVMFloatTypeKind: 
		case LLVMFP128TypeKind:
		case LLVMDoubleTypeKind:
			return true;
		default: return false;
	}
}

LLVMValueRef genBinaryExpression(LLVMCodeGenerator *self, BinaryExpr *expr) {
	LLVMValueRef lhs = genExpression(self, expr->lhand);
	LLVMValueRef rhs = genExpression(self, expr->lhand);
	if (!lhs || !rhs) {
		genError("Invalid expression");
		// TODO
	}

	bool floating = false; // isFloatingType(lhs) || isFloatingType(rhs)

	if (!strcmp(expr->binaryOp, "+")) {
		return floating ? LLVMBuildFAdd(self->builder, lhs, rhs, "add") : LLVMBuildAdd(self->builder, lhs, rhs, "add");
	}
	else if (!strcmp(expr->binaryOp, "-")) {
		return floating ? LLVMBuildFSub(self->builder, lhs, rhs, "sub") : LLVMBuildSub(self->builder, lhs, rhs, "add");
	}
	else if (!strcmp(expr->binaryOp, "*")) {
		return floating ? LLVMBuildFMul(self->builder, lhs, rhs, "mul") : LLVMBuildMul(self->builder, lhs, rhs, "mul");
	}
	else if (!strcmp(expr->binaryOp, "/")) {
		return floating ? LLVMBuildFDiv(self->builder, lhs, rhs, "div") : LLVMBuildUDiv(self->builder, lhs, rhs, "div");
	}
}

LLVMValueRef genFunctionCall(LLVMCodeGenerator *self, Call *call) {
	char *funcName = getVectorItem(call->callee, 0);
	LLVMValueRef func = LLVMGetNamedFunction(self->currentSourceFile->module, funcName);

	if (!func) {
		genError("Function not found in module");
		return false;
	}

	if (LLVMCountParams(func) != call->arguments->size) {
		genError("Function has too many/few arguments");
		return false;
	}

	LLVMValueRef *args = malloc(sizeof(LLVMValueRef) * call->arguments->size);
	for (int i = 0; i < call->arguments->size; i++) {
		Expression *expr = getVectorItem(call->arguments, i);
		args[i] = genExpression(self, expr);

		if (!args[i]) {
			genError("Could not evaluate argument in function call %s", funcName);
			free(args);
			return false;
		}
	}

	LLVMBuildCall(self->builder, func, args, call->arguments->size, "");
}

LLVMValueRef genTypeName(LLVMCodeGenerator *self, TypeName *name) {
	if (name->dataType == UNKNOWN_TYPE) {
		VariableReference *ref = NULL;
		if (hashmap_get(self->namedValues, name->name, (void**) &ref) == MAP_OK) {
			return ref->value;
		}
	}
	else {
		int typeAsEnum = getTypeFromString(name->name);
		return getLLVMType(typeAsEnum);
	}
}

LLVMValueRef genLiteral(LLVMCodeGenerator *self, Literal *lit) {
	switch (lit->type) {
		case INT_LITERAL_NODE: 
			return LLVMConstInt(getIntType(), lit->intLit->value, false);
		case FLOAT_LITERAL_NODE:
			return LLVMConstReal(LLVMFloatType(), lit->intLit->value);
		case STRING_LITERAL_NODE: {
			// this is kind of wrong
			size_t stringLength = strlen(lit->stringLit->value);
			LLVMValueRef str = LLVMAddGlobal(self->currentSourceFile->module, LLVMArrayType(LLVMInt8Type(), stringLength), "");
			LLVMSetLinkage(str, LLVMInternalLinkage);
  			LLVMSetGlobalConstant(str, true);
  			LLVMSetInitializer(str, LLVMConstString(lit->stringLit->value, stringLength, true));
  			return str;
		}
		default:
			printf("hmm?\n");
			break;
	}
	return false;
}

LLVMValueRef genTypeLit(LLVMCodeGenerator *self, TypeLit *lit) {
	switch (lit->type) {
		default:
			printf("abc: %s\n", getNodeTypeName(lit->type));
			break;
	}
}

LLVMValueRef genType(LLVMCodeGenerator *self, Type *type) {
	switch (type->type) {
		case TYPE_NAME_NODE: {
			return genTypeName(self, type->typeName);
		}
		case TYPE_LIT_NODE: {
			return genTypeLit(self, type->typeLit);
		}
		default:
			printf("todo\n");
			break;
	}
}
 
LLVMValueRef genExpression(LLVMCodeGenerator *self, Expression *expr) {
	switch (expr->exprType) {
		case TYPE_NODE: return genType(self, expr->type);
		case LITERAL_NODE: return genLiteral(self, expr->lit);
		case BINARY_EXPR_NODE: return genBinaryExpression(self, expr->binary);
		case UNARY_EXPR_NODE: printf("unary\n"); break;
		case FUNCTION_CALL_NODE: return genFunctionCall(self, expr->call);
		case ARRAY_INITIALIZER_NODE: printf("array init\n"); break;
		case ARRAY_INDEX_NODE: printf("array index\n"); break;
		case ALLOC_NODE: printf("alloc\n"); break;
		case SIZEOF_NODE: printf("sizeof\n"); break;
		default:
			errorMessage("Unknown node in expression %d", expr->exprType);
			break;
	}
}

LLVMValueRef genFunctionSignature(LLVMCodeGenerator *self, FunctionDecl *decl) {
	// store arguments from func signature
	FunctionSignature *signature = decl->signature;
	unsigned int argCount = signature->parameters->paramList->size;

	// lookup func
	LLVMValueRef func = LLVMGetNamedFunction(self->currentSourceFile->module, signature->name);
	if (func) {
		if (LLVMCountParams(func) != argCount) {
			genError("Function exists with different function signature");
			return false;
		}
	}
	else {
		// set llvm params
		LLVMTypeRef *params = safeMalloc(sizeof(LLVMTypeRef) * argCount);
		for (int i = 0; i < argCount; i++) {
			ParameterSection *param = getVectorItem(signature->parameters->paramList, i);
			char *name = NULL;
			bool isPointer = false;
			if (param->type->typeLit) {
				if (param->type->typeLit->type == POINTER_TYPE_NODE) {
					name = param->type->typeLit->pointerType->type->typeName->name;
					isPointer = true;
				}
			}
			else {
				name = param->type->typeName->name;
			}
			int type = getTypeFromString(name);
			params[i] = isPointer ? LLVMPointerType(getLLVMType(type), 0) : getLLVMType(type);
		}

		// create func prototype and add it to the module
		int funcType = getTypeFromString(signature->type->typeName->name);
		LLVMTypeRef funcTypeRef = LLVMFunctionType(getLLVMType(funcType), params, argCount, false);
		func = LLVMAddFunction(self->currentSourceFile->module, signature->name, funcTypeRef);
		if (decl->signature->isExtern) {
			LLVMSetLinkage(func, LLVMExternalLinkage);
		}

		for (int i = 0; i < argCount; i++) {
			LLVMValueRef param = LLVMGetParam(func, i);
			ParameterSection *paramSection = getVectorItem(signature->parameters->paramList, i);
			LLVMSetValueName(param, paramSection->name);

			VariableReference *ref = createVariableRef(paramSection->name);
			ref->value = param;
			hashmap_put(self->namedValues, paramSection->name, ref);
		}
	}

	return func;
}

LLVMValueRef genStatement(LLVMCodeGenerator *self, Statement *stmt) {
	switch (stmt->type) {
		case UNSTRUCTURED_STATEMENT_NODE: 
			return genUnstructuredStatementNode(self, stmt->unstructured);
		case STRUCTURED_STATEMENT_NODE:
			return genStructuredStatementNode(self, stmt->structured);
		case MACRO_NODE: 
			printf("macro ignored\n");
			consumeAstNode(self); 
			break; // ignore the macro
		default:
			errorMessage("Unknown statement %s\n", getNodeTypeName(stmt->type));
			break;
	}
	printf("returned null idk why\n");
	return false;
}

LLVMValueRef genFunctionDecl(LLVMCodeGenerator *self, FunctionDecl *decl) {
	self->currentSourceFile->scope++;

	LLVMValueRef prototype = genFunctionSignature(self, decl);
	if (!prototype) {
		genError("hmm");
		return NULL;
	}

	if (!decl->prototype) {
		LLVMBasicBlockRef block = LLVMAppendBasicBlock(prototype, "entry");
		LLVMPositionBuilderAtEnd(self->builder, block);

		// generate all the statements n that
		for (int i = 0; i < decl->body->stmtList->stmts->size; i++) {
			LLVMValueRef body = genStatement(self, getVectorItem(decl->body->stmtList->stmts, i));
		}
	}

	self->currentSourceFile->scope--;
	return prototype;
}

LLVMValueRef genVariableDecl(LLVMCodeGenerator *self, VariableDecl *decl) {
	int scope = self->currentSourceFile->scope;

	if (scope == GLOBAL_SCOPE) {
		LLVMValueRef expr = genExpression(self, decl->expr);
		if (expr) {
			LLVMValueRef glob = LLVMAddGlobal(self->currentSourceFile->module, genType(self, decl->type), decl->name);
			LLVMSetGlobalConstant(glob, !decl->mutable);
		}
		else {
			genError("Invalid expr");
		}
	}
	else {
		LLVMValueRef alloc = LLVMBuildAlloca(self->builder, genType(self, decl->type), decl->name);
		VariableReference *ref = createVariableRef(decl->name);
		ref->value = alloc;
		hashmap_put(self->namedValues, decl->name, ref);

		if (decl->expr) {
			LLVMBuildStore(self->builder, genExpression(self, decl->expr), alloc);
		}
	}
}

LLVMValueRef genDeclaration(LLVMCodeGenerator *self, Declaration *decl) {
	switch (decl->type) {
		case FUNCTION_DECL_NODE: return genFunctionDecl(self, decl->funcDecl);
		case VARIABLE_DECL_NODE: return genVariableDecl(self, decl->varDecl);
	}
}

LLVMValueRef genLeaveStatNode(LLVMCodeGenerator *self, LeaveStat *leave) {
	switch (leave->type) {
		case RETURN_STAT_NODE: {
			LLVMValueRef expr = NULL;
			if (leave->retStmt->expr) {
				expr = genExpression(self, leave->retStmt->expr);
			}
			LLVMBuildRet(self->builder, expr);
			break;
		}
	}
}

LLVMValueRef genUnstructuredStatementNode(LLVMCodeGenerator *self, UnstructuredStatement *stmt) {
	switch (stmt->type) {
		case DECLARATION_NODE: return genDeclaration(self, stmt->decl);
		case EXPR_STAT_NODE: return genExpression(self, stmt->expr); 
		case LEAVE_STAT_NODE: return genLeaveStatNode(self, stmt->leave);
		case FUNCTION_CALL_NODE: return genFunctionCall(self, stmt->call);
		default: 
			printf("found %s\n", getNodeTypeName(stmt->type));
			break;
	}
	return false;
}

LLVMValueRef genStructuredStatementNode(LLVMCodeGenerator *self, StructuredStatement *stmt) {
	switch (stmt->type) {
		default:
			printf("omg!\n");
			break;
	}
	return false;
}

void traverseAST(LLVMCodeGenerator *self) {
	for (int i = 0; i < self->abstractSyntaxTree->size; i++) {
		Statement *stmt = getVectorItem(self->abstractSyntaxTree, i);
		genStatement(self, stmt);
	}
}

char *createBitcode(LLVMCodeGenerator *self) {
	sds bitcodeFilename = sdsempty();
	bitcodeFilename = sdscat(bitcodeFilename, self->currentSourceFile->name);
	bitcodeFilename = sdscat(bitcodeFilename, ".bc");
	
	char *error = NULL;
	int verify_result = LLVMVerifyModule(self->currentSourceFile->module, LLVMReturnStatusAction, &error);
	if (verify_result) {
		genError("%s", error);
	} 
	else if (LLVMWriteBitcodeToFile(self->currentSourceFile->module, bitcodeFilename)) {
		genError("Failed to write bit-code");
	}
	LLVMDisposeMessage(error);
	return bitcodeFilename;
}

void convertBitcodeToAsm(LLVMCodeGenerator *self, sds bitcodeName) {
	sds asmFilename = sdsempty();
	asmFilename = sdscat(asmFilename, bitcodeName);
	asmFilename = sdscat(asmFilename, ".s");
	
	// convert bitcode files to assembly files
	sds toAsmCommand = sdsempty();
	toAsmCommand = sdscat(toAsmCommand, "llc ");
	toAsmCommand = sdscat(toAsmCommand, bitcodeName);
	toAsmCommand = sdscat(toAsmCommand, " -o ");
	toAsmCommand = sdscat(toAsmCommand, asmFilename);
	
	FILE *pipe = popen(toAsmCommand, "r");
	if (!pipe) {
		genError("Couldn't assemble bitcode file %s", bitcodeName);
	}
	pclose(pipe);

	int removeBitcodeResult = remove(bitcodeName);
	if (removeBitcodeResult) {
		genError("Couldn't remove bitcode file %s", bitcodeName);
	}

	pushBackItem(self->asmFiles, asmFilename);
	
	sdsfree(bitcodeName);
	sdsfree(toAsmCommand);
}

void createBinary(LLVMCodeGenerator *self) {
	sds linkCommand = sdsempty();
	linkCommand = sdscat(linkCommand, COMPILER);
	linkCommand = sdscat(linkCommand, " ");
	
	// get all the asm files to compile
	for (int i = 0; i < self->asmFiles->size; i++) {
		linkCommand = sdscat(linkCommand, getVectorItem(self->asmFiles, i));
		linkCommand = sdscat(linkCommand, " ");
	}
	
	linkCommand = sdscat(linkCommand, " -o ");
	linkCommand = sdscat(linkCommand, OUTPUT_EXECUTABLE_NAME);
	
	FILE *pipe = popen(linkCommand, "r");
	if (!pipe) {
		genError("Couldn't link object files");
	}
	pclose(pipe);

	sdsfree(linkCommand);
	
	for (int i = 0; i < self->asmFiles->size; i++) {
		int freeAsmResult = remove(getVectorItem(self->asmFiles, i));
		if (freeAsmResult) {
			genError("Couldn't remove assembly file %s", getVectorItem(self->asmFiles, i));
		}
		sdsfree(getVectorItem(self->asmFiles, i));
	}
}

void startLLVMCodeGeneration(LLVMCodeGenerator *self) {
	self->asmFiles = createVector(VECTOR_EXPONENTIAL);
	
	// loop through all of the source files and codegen
	// then compiler or whatever
	for (int i = 0; i < self->sourceFiles->size; i++) {
		SourceFile *sf = getVectorItem(self->sourceFiles, i);
		self->currentNode = 0;
		self->currentSourceFile = sf;
		self->abstractSyntaxTree = self->currentSourceFile->ast;
		// each file gets a module
		sf->module = LLVMModuleCreateWithName(sf->name);

		// this starts the codegen
		traverseAST(self);

		// just dump mods for now
		LLVMDumpModule(sf->module);
		
		// create bitcode stuff then convert to asm
		sds bitcodeName = createBitcode(self);
		convertBitcodeToAsm(self, bitcodeName);
	}

	// create the binary
	createBinary(self);

	// cleanup vector
	destroyVector(self->asmFiles);
}

LLVMTypeRef getIntType() {
	switch (sizeof(int)) {
		case 2: return LLVMInt16Type();
		case 4: return LLVMInt32Type();
		case 8: return LLVMInt64Type();
		default:
			// either something fucked up, or we're in the future on 128 bit machines
			verboseModeMessage("You have some wacky-sized int type, switching to 16 bit for default!");
			return LLVMInt16Type();
	}
}

LLVMTypeRef getLLVMType(DataType type) {
	switch (type) {
		case INT_128_TYPE:
		case UINT_128_TYPE:
			return LLVMIntType(128);
			
		case INT_64_TYPE:
		case UINT_64_TYPE:
			return LLVMInt64Type();
			
		case INT_32_TYPE:
		case UINT_32_TYPE:
			return LLVMInt32Type();
			
		case INT_16_TYPE:
		case UINT_16_TYPE:
			return LLVMInt16Type();
			
		case INT_8_TYPE:
		case UINT_8_TYPE:
			return LLVMInt8Type();
		
		case FLOAT_128_TYPE:
			return LLVMFP128Type();
		
		case FLOAT_64_TYPE:
			return LLVMDoubleType();
			
		case FLOAT_32_TYPE:
			return LLVMFloatType();
			
		case INT_TYPE:
			return getIntType();
			
		case BOOL_TYPE:
			return LLVMInt1Type();
			
		case BYTE_TYPE:
			return LLVMInt1Type();

		case CHAR_TYPE:
			// genError("Char type unimplemented");
			return LLVMInt8Type(); // for now its i8 -- gonna get replaced
			
		case VOID_TYPE:
			return LLVMVoidType();
			
		case UNKNOWN_TYPE:
			genError("Unknown type %d\n", type);
			return NULL;
	}
}
