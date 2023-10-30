// Declares clang::SyntaxOnlyAction.
#include "clang/Frontend/FrontendActions.h"
#include "clang/Tooling/CommonOptionsParser.h"
#include "clang/Tooling/Tooling.h"
// Declares llvm::cl::extrahelp.
#include "llvm/Support/CommandLine.h"
#include "clang/ASTMatchers/ASTMatchers.h"
#include "clang/ASTMatchers/ASTMatchFinder.h"

#include <set>

using namespace clang;
using namespace clang::ast_matchers;
using namespace clang::tooling;
using namespace llvm;

// Apply a custom category to all command-line options so that they are the
// only ones displayed.
static llvm::cl::OptionCategory MyToolCategory("my-tool options");

// CommonOptionsParser declares HelpMessage with a description of the common
// command-line options related to the compilation database and input files.
// It's nice to have this help message in all tools.
static cl::extrahelp CommonHelp(CommonOptionsParser::HelpMessage);

// A help message for this specific tool can be added afterwards.
static cl::extrahelp MoreHelp("\nMore help text...\n");

static llvm::cl::opt<int> StartLine("start_line", cl::init(0), cl::desc("the line to start at"), cl::value_desc("start line for parsing"));
static llvm::cl::opt<int> EndLine("end_line", cl::init(0), cl::desc("the line to end at"), cl::value_desc("end line for parsing"));

std::set<std::string> unsafeFunctionNames;

// STATS
unsigned long g_pointers = 0;
unsigned long g_voidPointers = 0;
unsigned long g_uninitializedPointers = 0;
unsigned long g_smartPointers = 0;
unsigned long g_implicitMallocs = 0;
unsigned long g_mallocs = 0;
unsigned long g_frees = 0;
unsigned long g_unsafeFunctions = 0;
unsigned long g_unsafeCasts = 0;
unsigned long g_constructFromImplicits = 0;
unsigned long g_unions = 0;
unsigned long g_refClassMembers = 0;
unsigned long g_refReturns = 0;
unsigned long g_constRefReturns = 0;
unsigned long g_refToDerefs = 0;
unsigned long g_addressOfArrayElements = 0;
unsigned long g_twoDArrays = 0;

#define ANY_POINTER_OPERAND anyOf(declRefExpr(to(valueDecl(hasType(isAnyPointer())).bind("pointer"))),implicitCastExpr(hasSourceExpression(declRefExpr(to(valueDecl(hasType(isAnyPointer())).bind("pointer"))))))

DeclarationMatcher PointerDeclMatcher =
  valueDecl(
	    hasType(isAnyPointer())
	    ).bind("pointerValue");

DeclarationMatcher SmartPointerMatcher =
  declaratorDecl(
		 hasType(
			 cxxRecordDecl(
				       hasAnyName("unique_ptr",
						  "shared_ptr",
						  "weak_ptr",
						  "auto_ptr",
						  "std::unique_ptr",
						  "std::shared_ptr",
						  "std::weak_ptr",
						  "std::auto_ptr",
						  "ptr")
				       )
			 )
	    ).bind("smartPointer");

DeclarationMatcher RefToDerefMatcher =
  varDecl(
	  hasType(
		  referenceType()
		  )
	  , hasInitializer(
	  	   unaryOperator(
	  				 hasOperatorName("*")
				 )
	  		   )
	  ).bind("var");

StatementMatcher ArraySubscriptMatcher =
  arraySubscriptExpr(hasBase(ANY_POINTER_OPERAND)).bind("subscriptExpr");

StatementMatcher TwoDArrayMatcher =
  arraySubscriptExpr(
		     hasAncestor(
				 arraySubscriptExpr()
				 )
		     ).bind("subscriptExpr");

StatementMatcher PointerArithmeticMatcher =
  anyOf(
    binaryOperator(
      allOf(
        hasLHS(ANY_POINTER_OPERAND),
        hasRHS(binaryOperator(allOf(
          hasAnyOperatorName("+","-","*","/","<<",">>"),
	  hasEitherOperand(ANY_POINTER_OPERAND)))
        )
     )
  ),
    unaryOperator(
      allOf(
        hasAnyOperatorName("++","--"),
        hasUnaryOperand(ANY_POINTER_OPERAND)
      )
    )
);

DeclarationMatcher PointerArrayInitMatcher =
  varDecl(hasInitializer(cxxNewExpr(isArray()))).bind("pointer");

DeclarationMatcher UnionMatcher =
  tagDecl(isUnion()).bind("tag");

DeclarationMatcher RefClassMemberMatcher =
  fieldDecl(hasType(referenceType()),
	    hasAncestor(tagDecl(isClass()))).bind("refvar");

DeclarationMatcher ReturnMatcher =
  functionDecl(
	       returns(
		       referenceType()
		       )
	       ).bind("function");

StatementMatcher CallMatcher =
  callExpr(
	   forEachArgumentWithParam(
				    ANY_POINTER_OPERAND,
				    parmVarDecl(hasType(isAnyPointer())).bind("paramPointer")
				    )
	   ).bind("call");

StatementMatcher MallocVoidMatcher =
  callExpr(
	   callee(
		  functionDecl(
			       hasName("malloc")
			       )
		  ),
	   hasParent(implicitCastExpr())
	   ).bind("mallocVoid");

StatementMatcher MallocRegMatcher =
  callExpr(
	   callee(
		  functionDecl(
			       hasName("malloc")
			       )
		  )
	   ).bind("malloc");

StatementMatcher UnsafeCastMatcher =
  anyOf(
	cStyleCastExpr().bind("cast"),
	cxxReinterpretCastExpr().bind("cast")
	);

StatementMatcher AddressOfArrayMatcher =
  unaryOperator(
		hasOperatorName("&"),
		hasUnaryOperand(
				arraySubscriptExpr()
				)
		).bind("op");

StatementMatcher ConstructImplicitMatcher =
  implicitCastExpr(
		   hasAncestor(
			       cxxConstructExpr().bind("constructor")
			       )
		   );

std::set<int64_t> arrayPointers;
bool didFunctionID = false;
std::string fileName;
bool didIncludeInit = false;
SourceRange mainRange;

bool shouldIncludeLocation(ASTContext *Context, const SourceLocation &loc){
  /*
  if(!didIncludeInit){
    SourceManager& SM = Context->getSourceManager();
    FileManager& FM = SM.getFileManager();
    auto file_entry_ptr = FM.getFile(fileName);
    SourceLocation startLoc = SM.translateFileLineCol(file_entry_ptr.get(), StartLine, 0);
    SourceLocation endLoc = SM.translateFileLineCol(file_entry_ptr.get(), EndLine, 0);

    mainRange.setBegin(startLoc);
    mainRange.setEnd(endLoc);

    didIncludeInit = true;
  }
  
  //return Context->getSourceManager().isWrittenInMainFile(loc);
  SourceRange range(loc,loc);
  return mainRange.fullyContains(range);
  */

  SourceManager& SM = Context->getSourceManager();
  return SM.isWrittenInMainFile(loc);
}

class ArraySubscriptIdentifier : public MatchFinder::MatchCallback {
public :
  virtual void run(const MatchFinder::MatchResult &Result) override {
    ASTContext *Context = Result.Context;
    const ValueDecl *AD = Result.Nodes.getNodeAs<ValueDecl>("pointer");
    if(!AD || !shouldIncludeLocation(Context,AD->getLocation()))
      return;

    arrayPointers.insert(AD->getID());
  } 
};

class TwoDArrayFinder : public MatchFinder::MatchCallback {
public :
  virtual void run(const MatchFinder::MatchResult &Result) override {
    ASTContext *Context = Result.Context;
    const ArraySubscriptExpr *AD = Result.Nodes.getNodeAs<ArraySubscriptExpr>("subscriptExpr");
    if(!AD || !shouldIncludeLocation(Context,AD->getBeginLoc()))
      return;

    g_twoDArrays++;
  } 
};

class AddressOfArrayFinder : public MatchFinder::MatchCallback {
public :
  virtual void run(const MatchFinder::MatchResult &Result) override {
    ASTContext *Context = Result.Context;
    const UnaryOperator *op = Result.Nodes.getNodeAs<UnaryOperator>("op");
    if(!op || !shouldIncludeLocation(Context,op->getBeginLoc()))
      return;

    g_addressOfArrayElements++;
  } 
};

class PointerArithmeticIdentifier : public MatchFinder::MatchCallback {
public :
  virtual void run(const MatchFinder::MatchResult &Result) override {
    ASTContext *Context = Result.Context;
    const ValueDecl *AD = Result.Nodes.getNodeAs<ValueDecl>("pointer");
    if(!AD || !shouldIncludeLocation(Context,AD->getLocation()))
      return;

    arrayPointers.insert(AD->getID());
  } 
};

class PointerArrayInitIdentifier : public MatchFinder::MatchCallback {
public :
  virtual void run(const MatchFinder::MatchResult &Result) override {
    ASTContext *Context = Result.Context;
    const VarDecl *AD = Result.Nodes.getNodeAs<VarDecl>("pointer");
    if(!AD || !shouldIncludeLocation(Context,AD->getLocation()))
      return;

    arrayPointers.insert(AD->getID());
  } 
};

class CallIdentifier : public MatchFinder::MatchCallback {
public :
  virtual void run(const MatchFinder::MatchResult &Result) override {
    const CallExpr *CE = Result.Nodes.getNodeAs<CallExpr>("call");
    const ValueDecl *Arg = Result.Nodes.getNodeAs<ValueDecl>("pointer");
    const ValueDecl *Param = Result.Nodes.getNodeAs<ValueDecl>("paramPointer");
    
    if(!Arg || !Param)
      return;

    if(arrayPointers.count(Param->getID()) > 0){    
      arrayPointers.insert(Arg->getID());
    }
    if(arrayPointers.count(Arg->getID()) > 0){
      arrayPointers.insert(Param->getID());
    }

    if(didFunctionID)
      return;    
    
    if(!CE)
      return;

    const FunctionDecl *FD = CE->getDirectCallee();
    if(FD){
      if(unsafeFunctionNames.count(FD->getNameAsString())){
	g_unsafeFunctions++;
      }
      if(FD->getNameAsString().compare("free") == 0){
	g_frees++;
      }
    }
  } 
};

class MallocVMatcher : public MatchFinder::MatchCallback {
public:
  virtual void run(const MatchFinder::MatchResult &Result) override {
    ASTContext *Context = Result.Context;
    const CallExpr *CE = Result.Nodes.getNodeAs<CallExpr>("mallocVoid");
    if(!CE || !shouldIncludeLocation(Context,CE->getBeginLoc()))
      return;

    // Should fail on explicit casts from malloc due to (CStyleCastExpr instead)
    g_implicitMallocs++;
  }
};

class MallocMatcher : public MatchFinder::MatchCallback {
public:
  virtual void run(const MatchFinder::MatchResult &Result) override {
    ASTContext *Context = Result.Context;
    const CallExpr *CE = Result.Nodes.getNodeAs<CallExpr>("malloc");
    if(!CE || !shouldIncludeLocation(Context,CE->getBeginLoc()))
      return;

    // Should fail on explicit casts from malloc due to (CStyleCastExpr instead)
    g_mallocs++;
  }
};

class PointerFinder : public MatchFinder::MatchCallback {
public :
  virtual void run(const MatchFinder::MatchResult &Result) override {
    ASTContext *Context = Result.Context;
    const ValueDecl *VD = Result.Nodes.getNodeAs<ValueDecl>("pointerValue");
    // We do not want to convert header files!
    if (!VD || !shouldIncludeLocation(Context,VD->getLocation()))
      return;

    if(!VD->getType().isNull() && VD->getType()->isVoidPointerType()){
      g_voidPointers++;
    }

    const VarDecl *VarD = dyn_cast<const VarDecl>(VD);
    if(VarD && !VarD->hasInit()){
      g_uninitializedPointers++;
    }

    g_pointers++;
  }
};

class SmartPointerFinder : public MatchFinder::MatchCallback {
public :
  virtual void run(const MatchFinder::MatchResult &Result) override {
    ASTContext *Context = Result.Context;
    const DeclaratorDecl *VD = Result.Nodes.getNodeAs<DeclaratorDecl>("smartPointer");
    // We do not want to convert header files!
    if (!VD || !shouldIncludeLocation(Context,VD->getLocation()))
      return;

    g_smartPointers++;
  }
};

class RefToDerefFinder : public MatchFinder::MatchCallback {
public :
  virtual void run(const MatchFinder::MatchResult &Result) override {
    ASTContext *Context = Result.Context;
    const VarDecl *VD = Result.Nodes.getNodeAs<VarDecl>("var");
    // We do not want to convert header files!
    if (!VD || !shouldIncludeLocation(Context,VD->getLocation()))
      return;

    g_refToDerefs++;
  }
};

class UnsafeCastFinder : public MatchFinder::MatchCallback {
public :
  virtual void run(const MatchFinder::MatchResult &Result) override {
    ASTContext *Context = Result.Context;
    const CastExpr *VD = Result.Nodes.getNodeAs<CastExpr>("cast");
    // We do not want to convert header files!
    if (!VD || !shouldIncludeLocation(Context,VD->getBeginLoc()))
      return;
    
    g_unsafeCasts++;
  }
};

class ConstructImplicitFinder : public MatchFinder::MatchCallback {
public :
  virtual void run(const MatchFinder::MatchResult &Result) override {
    ASTContext *Context = Result.Context;
    const CXXConstructExpr *VD = Result.Nodes.getNodeAs<CXXConstructExpr>("constructor");
    // We do not want to convert header files!
    if (!VD || !shouldIncludeLocation(Context,VD->getLocation()))
      return;
    
    g_constructFromImplicits++;
  }
};

class UnionFinder : public MatchFinder::MatchCallback {
public :
  virtual void run(const MatchFinder::MatchResult &Result) override {
    ASTContext *Context = Result.Context;
    const TagDecl *VD = Result.Nodes.getNodeAs<TagDecl>("tag");
    // We do not want to convert header files!
    if (!VD || !shouldIncludeLocation(Context,VD->getBeginLoc()))
      return;

    g_unions++;
  }
};

class RefClassMemberFinder : public MatchFinder::MatchCallback {
public :
  virtual void run(const MatchFinder::MatchResult &Result) override {
    ASTContext *Context = Result.Context;
    const FieldDecl *VD = Result.Nodes.getNodeAs<FieldDecl>("refvar");
    // We do not want to convert header files!
    if (!VD || !shouldIncludeLocation(Context,VD->getBeginLoc()))
      return;

    g_refClassMembers++;
  }
};

class ReturnFinder : public MatchFinder::MatchCallback {
public :
  virtual void run(const MatchFinder::MatchResult &Result) override {
    ASTContext *Context = Result.Context;
    const FunctionDecl *FD = Result.Nodes.getNodeAs<FunctionDecl>("function");
    // We do not want to convert header files!
    if (!FD || !shouldIncludeLocation(Context,FD->getBeginLoc()))
      return;

    QualType returnType = FD->getReturnType();
    QualType referencedType = returnType.getNonReferenceType();
    
    if(referencedType.isConstQualified()){
      g_constRefReturns++;
    }else{
      g_refReturns++;
    }
  }
};

int main(int argc, const char **argv) {
  auto ExpectedParser = CommonOptionsParser::create(argc, argv, MyToolCategory);
  if (!ExpectedParser) {
    // Fail gracefully for unsupported options.
    llvm::errs() << ExpectedParser.takeError();
    return 1;
  }
  CommonOptionsParser& OptionsParser = ExpectedParser.get();
  ClangTool Tool(OptionsParser.getCompilations(),
                 OptionsParser.getSourcePathList());

  fileName = OptionsParser.getSourcePathList()[0];
  
  unsafeFunctionNames.insert("memcpy");
  unsafeFunctionNames.insert("memmove");
  unsafeFunctionNames.insert("memset");
  unsafeFunctionNames.insert("memcmp");
  unsafeFunctionNames.insert("memchr");
  unsafeFunctionNames.insert("strcpy");
  unsafeFunctionNames.insert("strlen");
  unsafeFunctionNames.insert("strcat");
  unsafeFunctionNames.insert("strncat");
  unsafeFunctionNames.insert("strcmp");
  unsafeFunctionNames.insert("strncmp");
  unsafeFunctionNames.insert("strncpy");
  unsafeFunctionNames.insert("strchr");
  unsafeFunctionNames.insert("strcoll");
  unsafeFunctionNames.insert("strcspn");
  unsafeFunctionNames.insert("strpbrk");
  unsafeFunctionNames.insert("strrchr");
  unsafeFunctionNames.insert("strspn");
  unsafeFunctionNames.insert("strstr");
  unsafeFunctionNames.insert("strtok");
  unsafeFunctionNames.insert("strxfrm");
  unsafeFunctionNames.insert("gets");
  
  // First, identify array and singleton pointers
  ArraySubscriptIdentifier SubID;
  PointerArithmeticIdentifier ArithID;
  PointerArrayInitIdentifier ArrayInitID;
  MallocVMatcher MallocVMatch;
  MallocMatcher MallocMatch;
  UnsafeCastFinder UnsafeCastMatch;
  UnionFinder UnionID;
  RefClassMemberFinder RefClassID;
  ReturnFinder ReturnID;
  SmartPointerFinder SmartPointerID;
  AddressOfArrayFinder AddressOfArrayID;
  ConstructImplicitFinder ConstructImplicitID;
  RefToDerefFinder RefToDerefID;
  TwoDArrayFinder TwoDArrayID;
  MatchFinder IdentifyFinder;
  
  IdentifyFinder.addMatcher(ArraySubscriptMatcher, &SubID);
  IdentifyFinder.addMatcher(PointerArithmeticMatcher, &ArithID);
  IdentifyFinder.addMatcher(PointerArrayInitMatcher, &ArrayInitID);
  IdentifyFinder.addMatcher(MallocVoidMatcher, &MallocVMatch);
  IdentifyFinder.addMatcher(MallocRegMatcher, &MallocMatch);
  IdentifyFinder.addMatcher(UnsafeCastMatcher, &UnsafeCastMatch);
  IdentifyFinder.addMatcher(UnionMatcher, &UnionID);
  IdentifyFinder.addMatcher(RefClassMemberMatcher, &RefClassID);
  IdentifyFinder.addMatcher(ReturnMatcher, &ReturnID);
  IdentifyFinder.addMatcher(AddressOfArrayMatcher, &AddressOfArrayID);
  IdentifyFinder.addMatcher(SmartPointerMatcher, &SmartPointerID);
  IdentifyFinder.addMatcher(ConstructImplicitMatcher, &ConstructImplicitID);
  IdentifyFinder.addMatcher(TwoDArrayMatcher, &TwoDArrayID);
  IdentifyFinder.addMatcher(RefToDerefMatcher, &RefToDerefID);
  
  Tool.run(newFrontendActionFactory(&IdentifyFinder).get());

  // Next, repeatedly check for pointers used as arguments to array parameters
  size_t oldSize;
  do{
    oldSize = arrayPointers.size();
    CallIdentifier CallID;
    MatchFinder CallFinder;
    CallFinder.addMatcher(CallMatcher, &CallID);
    Tool.run(newFrontendActionFactory(&CallFinder).get());
    didFunctionID = true; // only match unsafe functions once
  }while(oldSize != arrayPointers.size());

  PointerFinder Finder;
  MatchFinder LastFinder;
  LastFinder.addMatcher(PointerDeclMatcher, &Finder);
  Tool.run(newFrontendActionFactory(&LastFinder).get());
  
  // Output stats
  // llvm::outs() << "Pointers: " << g_pointers << "\n";
  // llvm::outs() << "Void Pointers: " << g_voidPointers << "\n";
  // llvm::outs() << "UninitializedPointers: " << g_uninitializedPointers << "\n";
  // llvm::outs() << "Smart Pointers: " << g_smartPointers << "\n";
  // llvm::outs() << "Malloc: " << g_mallocs << "\n";
  // llvm::outs() << "Free: " << g_frees << "\n";
  // llvm::outs() << "Malloc (void*): " << g_implicitMallocs << "\n";
  // llvm::outs() << "Unsafe Functions: " << g_unsafeFunctions << "\n";
  // llvm::outs() << "Unsafe Casts: " << g_unsafeCasts << "\n";
  // llvm::outs() << "Construct from Implicit Cast: " << g_constructFromImplicits << "\n";
  // llvm::outs() << "Unions: " << g_unions << "\n";
  // llvm::outs() << "Reference class members: " << g_refClassMembers << "\n";
  // llvm::outs() << "Reference Returns: " << g_refReturns << "\n";
  // llvm::outs() << "const Reference Returns: " << g_constRefReturns << "\n";
  // llvm::outs() << "References to Dereferenced Pointers: " << g_refToDerefs << "\n";
  // llvm::outs() << "Address-of Array Elements: " << g_addressOfArrayElements << "\n";
  // llvm::outs() << "2D Arrays: " << g_twoDArrays << "\n";

  llvm::outs() << g_pointers << ",";
  llvm::outs() << g_voidPointers << ",";
  llvm::outs() << g_uninitializedPointers << ",";
  llvm::outs() << g_smartPointers << ",";
  llvm::outs() << g_mallocs << ",";
  llvm::outs() << g_frees << ",";
  llvm::outs() << g_implicitMallocs << ",";
  llvm::outs() << g_unsafeFunctions << ",";
  llvm::outs() << g_unsafeCasts << ",";
  llvm::outs() << g_constructFromImplicits << ",";
  llvm::outs() << g_unions << ",";
  llvm::outs() << g_refClassMembers << ",";
  llvm::outs() << g_refReturns << ",";
  llvm::outs() << g_constRefReturns << ",";
  llvm::outs() << g_refToDerefs << ",";
  llvm::outs() << g_addressOfArrayElements << ",";
  llvm::outs() << g_twoDArrays;
  
  return 0;
}
