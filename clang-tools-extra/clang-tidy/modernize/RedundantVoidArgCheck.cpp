//===- RedundantVoidArgCheck.cpp - clang-tidy -----------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "RedundantVoidArgCheck.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Lex/Lexer.h"

using namespace clang::ast_matchers;

namespace clang::tidy::modernize {

namespace {

// Determine if the given QualType is a nullary function or pointer to same.
bool protoTypeHasNoParms(QualType QT) {
  if (const auto *PT = QT->getAs<PointerType>())
    QT = PT->getPointeeType();
  if (auto *MPT = QT->getAs<MemberPointerType>())
    QT = MPT->getPointeeType();
  if (const auto *FP = QT->getAs<FunctionProtoType>())
    return FP->getNumParams() == 0;
  return false;
}

const char FunctionId[] = "function";
const char TypedefId[] = "typedef";
const char FieldId[] = "field";
const char VarId[] = "var";
const char NamedCastId[] = "named-cast";
const char CStyleCastId[] = "c-style-cast";
const char ExplicitCastId[] = "explicit-cast";
const char LambdaId[] = "lambda";

} // namespace

void RedundantVoidArgCheck::registerMatchers(MatchFinder *Finder) {
  Finder->addMatcher(functionDecl(parameterCountIs(0), unless(isImplicit()),
                                  unless(isInstantiated()), unless(isExternC()))
                         .bind(FunctionId),
                     this);
  Finder->addMatcher(typedefNameDecl(unless(isImplicit())).bind(TypedefId),
                     this);
  auto ParenFunctionType = parenType(innerType(functionType()));
  auto PointerToFunctionType = pointee(ParenFunctionType);
  auto FunctionOrMemberPointer =
      anyOf(hasType(pointerType(PointerToFunctionType)),
            hasType(memberPointerType(PointerToFunctionType)));
  Finder->addMatcher(fieldDecl(FunctionOrMemberPointer).bind(FieldId), this);
  Finder->addMatcher(varDecl(FunctionOrMemberPointer).bind(VarId), this);
  auto CastDestinationIsFunction =
      hasDestinationType(pointsTo(ParenFunctionType));
  Finder->addMatcher(
      cStyleCastExpr(CastDestinationIsFunction).bind(CStyleCastId), this);
  Finder->addMatcher(
      cxxStaticCastExpr(CastDestinationIsFunction).bind(NamedCastId), this);
  Finder->addMatcher(
      cxxReinterpretCastExpr(CastDestinationIsFunction).bind(NamedCastId),
      this);
  Finder->addMatcher(
      cxxConstCastExpr(CastDestinationIsFunction).bind(NamedCastId), this);
  Finder->addMatcher(lambdaExpr().bind(LambdaId), this);
}

void RedundantVoidArgCheck::check(const MatchFinder::MatchResult &Result) {
  const BoundNodes &Nodes = Result.Nodes;
  if (const auto *Function = Nodes.getNodeAs<FunctionDecl>(FunctionId))
    processFunctionDecl(Result, Function);
  else if (const auto *TypedefName =
               Nodes.getNodeAs<TypedefNameDecl>(TypedefId))
    processTypedefNameDecl(Result, TypedefName);
  else if (const auto *Member = Nodes.getNodeAs<FieldDecl>(FieldId))
    processFieldDecl(Result, Member);
  else if (const auto *Var = Nodes.getNodeAs<VarDecl>(VarId))
    processVarDecl(Result, Var);
  else if (const auto *NamedCast =
               Nodes.getNodeAs<CXXNamedCastExpr>(NamedCastId))
    processNamedCastExpr(Result, NamedCast);
  else if (const auto *CStyleCast =
               Nodes.getNodeAs<CStyleCastExpr>(CStyleCastId))
    processExplicitCastExpr(Result, CStyleCast);
  else if (const auto *ExplicitCast =
               Nodes.getNodeAs<ExplicitCastExpr>(ExplicitCastId))
    processExplicitCastExpr(Result, ExplicitCast);
  else if (const auto *Lambda = Nodes.getNodeAs<LambdaExpr>(LambdaId))
    processLambdaExpr(Result, Lambda);
}

void RedundantVoidArgCheck::processFunctionDecl(
    const MatchFinder::MatchResult &Result, const FunctionDecl *Function) {
  const auto *Method = dyn_cast<CXXMethodDecl>(Function);
  SourceLocation Start = Method && Method->getParent()->isLambda()
                             ? Method->getBeginLoc()
                             : Function->getLocation();
  SourceLocation End = Function->getEndLoc();
  if (Function->isThisDeclarationADefinition()) {
    if (const Stmt *Body = Function->getBody()) {
      End = Body->getBeginLoc();
      if (End.isMacroID() &&
          Result.SourceManager->isAtStartOfImmediateMacroExpansion(End))
        End = Result.SourceManager->getExpansionLoc(End);
      End = End.getLocWithOffset(-1);
    }
    removeVoidArgumentTokens(Result, SourceRange(Start, End),
                             "function definition");
  } else
    removeVoidArgumentTokens(Result, SourceRange(Start, End),
                             "function declaration");
}

static bool isMacroIdentifier(const IdentifierTable &Idents,
                              const Token &ProtoToken) {
  if (!ProtoToken.is(tok::TokenKind::raw_identifier))
    return false;

  IdentifierTable::iterator It = Idents.find(ProtoToken.getRawIdentifier());
  if (It == Idents.end())
    return false;

  return It->second->hadMacroDefinition();
}

void RedundantVoidArgCheck::removeVoidArgumentTokens(
    const ast_matchers::MatchFinder::MatchResult &Result, SourceRange Range,
    StringRef GrammarLocation) {
  CharSourceRange CharRange =
      Lexer::makeFileCharRange(CharSourceRange::getTokenRange(Range),
                               *Result.SourceManager, getLangOpts());

  std::string DeclText =
      Lexer::getSourceText(CharRange, *Result.SourceManager, getLangOpts())
          .str();
  Lexer PrototypeLexer(CharRange.getBegin(), getLangOpts(), DeclText.data(),
                       DeclText.data(), DeclText.data() + DeclText.size());
  enum class TokenState {
    Start,
    MacroId,
    MacroLeftParen,
    MacroArguments,
    LeftParen,
    Void,
  };
  TokenState State = TokenState::Start;
  Token VoidToken;
  Token ProtoToken;
  const IdentifierTable &Idents = Result.Context->Idents;
  int MacroLevel = 0;
  std::string Diagnostic =
      ("redundant void argument list in " + GrammarLocation).str();

  while (!PrototypeLexer.LexFromRawLexer(ProtoToken)) {
    switch (State) {
    case TokenState::Start:
      if (ProtoToken.is(tok::TokenKind::l_paren))
        State = TokenState::LeftParen;
      else if (isMacroIdentifier(Idents, ProtoToken))
        State = TokenState::MacroId;
      break;
    case TokenState::MacroId:
      if (ProtoToken.is(tok::TokenKind::l_paren))
        State = TokenState::MacroLeftParen;
      else
        State = TokenState::Start;
      break;
    case TokenState::MacroLeftParen:
      ++MacroLevel;
      if (ProtoToken.is(tok::TokenKind::raw_identifier)) {
        if (isMacroIdentifier(Idents, ProtoToken))
          State = TokenState::MacroId;
        else
          State = TokenState::MacroArguments;
      } else if (ProtoToken.is(tok::TokenKind::r_paren)) {
        --MacroLevel;
        if (MacroLevel == 0)
          State = TokenState::Start;
        else
          State = TokenState::MacroId;
      } else
        State = TokenState::MacroArguments;
      break;
    case TokenState::MacroArguments:
      if (isMacroIdentifier(Idents, ProtoToken))
        State = TokenState::MacroLeftParen;
      else if (ProtoToken.is(tok::TokenKind::r_paren)) {
        --MacroLevel;
        if (MacroLevel == 0)
          State = TokenState::Start;
      }
      break;
    case TokenState::LeftParen:
      if (ProtoToken.is(tok::TokenKind::raw_identifier)) {
        if (isMacroIdentifier(Idents, ProtoToken))
          State = TokenState::MacroId;
        else if (ProtoToken.getRawIdentifier() == "void") {
          State = TokenState::Void;
          VoidToken = ProtoToken;
        }
      } else if (ProtoToken.is(tok::TokenKind::l_paren))
        State = TokenState::LeftParen;
      else
        State = TokenState::Start;
      break;
    case TokenState::Void:
      State = TokenState::Start;
      if (ProtoToken.is(tok::TokenKind::r_paren))
        removeVoidToken(VoidToken, Diagnostic);
      else if (ProtoToken.is(tok::TokenKind::l_paren))
        State = TokenState::LeftParen;
      break;
    }
  }

  if (State == TokenState::Void && ProtoToken.is(tok::TokenKind::r_paren))
    removeVoidToken(VoidToken, Diagnostic);
}

void RedundantVoidArgCheck::removeVoidToken(Token VoidToken,
                                            StringRef Diagnostic) {
  SourceLocation VoidLoc = VoidToken.getLocation();
  diag(VoidLoc, Diagnostic) << FixItHint::CreateRemoval(VoidLoc);
}

void RedundantVoidArgCheck::processTypedefNameDecl(
    const MatchFinder::MatchResult &Result,
    const TypedefNameDecl *TypedefName) {
  if (protoTypeHasNoParms(TypedefName->getUnderlyingType()))
    removeVoidArgumentTokens(Result, TypedefName->getSourceRange(),
                             isa<TypedefDecl>(TypedefName) ? "typedef"
                                                           : "type alias");
}

void RedundantVoidArgCheck::processFieldDecl(
    const MatchFinder::MatchResult &Result, const FieldDecl *Member) {
  if (protoTypeHasNoParms(Member->getType()))
    removeVoidArgumentTokens(Result, Member->getSourceRange(),
                             "field declaration");
}

void RedundantVoidArgCheck::processVarDecl(
    const MatchFinder::MatchResult &Result, const VarDecl *Var) {
  if (protoTypeHasNoParms(Var->getType())) {
    SourceLocation Begin = Var->getBeginLoc();
    if (Var->hasInit()) {
      SourceLocation InitStart =
          Result.SourceManager->getExpansionLoc(Var->getInit()->getBeginLoc())
              .getLocWithOffset(-1);
      removeVoidArgumentTokens(Result, SourceRange(Begin, InitStart),
                               "variable declaration with initializer");
    } else
      removeVoidArgumentTokens(Result, Var->getSourceRange(),
                               "variable declaration");
  }
}

void RedundantVoidArgCheck::processNamedCastExpr(
    const MatchFinder::MatchResult &Result, const CXXNamedCastExpr *NamedCast) {
  if (protoTypeHasNoParms(NamedCast->getTypeAsWritten()))
    removeVoidArgumentTokens(
        Result,
        NamedCast->getTypeInfoAsWritten()->getTypeLoc().getSourceRange(),
        "named cast");
}

void RedundantVoidArgCheck::processExplicitCastExpr(
    const MatchFinder::MatchResult &Result,
    const ExplicitCastExpr *ExplicitCast) {
  if (protoTypeHasNoParms(ExplicitCast->getTypeAsWritten()))
    removeVoidArgumentTokens(Result, ExplicitCast->getSourceRange(),
                             "cast expression");
}

void RedundantVoidArgCheck::processLambdaExpr(
    const MatchFinder::MatchResult &Result, const LambdaExpr *Lambda) {
  if (Lambda->getLambdaClass()->getLambdaCallOperator()->getNumParams() == 0 &&
      Lambda->hasExplicitParameters()) {
    SourceManager *SM = Result.SourceManager;
    TypeLoc TL = Lambda->getLambdaClass()->getLambdaTypeInfo()->getTypeLoc();
    removeVoidArgumentTokens(Result,
                             {SM->getSpellingLoc(TL.getBeginLoc()),
                              SM->getSpellingLoc(TL.getEndLoc())},
                             "lambda expression");
  }
}

} // namespace clang::tidy::modernize
