// RUN: %target-swift-frontend %s -typecheck -verify

func badIs<T>(_ value: Any, anInstanceOf type: T.Type) -> Bool {
    value is type // expected-error {{use of undeclared type 'type'}}
}
