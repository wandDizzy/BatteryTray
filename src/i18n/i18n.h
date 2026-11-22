#pragma once
// Translations. Add a language: add a column in i18n.cpp and a case in SetLanguage().
#include <string>

void SetLanguage(const std::wstring& code);   // "auto" | "en" | "uk" | "ru"
std::wstring CurrentLanguage();               // resolved code
const wchar_t* T(const char* key);            // returns the key itself (as text) if missing
