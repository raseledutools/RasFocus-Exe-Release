// accounts.h
#pragma once

#include <windows.h>
#include <gdiplus.h>
#include <string>

// ==========================================
// 🌐 GLOBAL AUTHENTICATION STATE
// ==========================================
// এই ভেরিয়েবলগুলো extern করা হয়েছে, যাতে পুরো প্রজেক্টের 
// যেকোনো ফাইল থেকে ইউজারের লগইন স্ট্যাটাস চেক করা যায়।
extern bool g_isLoggedIn;
extern std::wstring g_userName;
extern std::wstring g_userEmail;
extern std::wstring g_userUID;
extern bool g_isPremiumUser;

// ==========================================
// 🎨 UI & EVENT FUNCTIONS
// ==========================================
// Accounts ট্যাবের ডিজাইন রেন্ডার করার ফাংশন
void DrawAccountsTab(Gdiplus::Graphics& g, float cx, float cy, float cw, float ch);

// মাউস হোভার এবং ক্লিক হ্যান্ডেল করার ফাংশন
void ProcessAccountsMouseMove(float x, float y);
void ProcessAccountsMouseClick(float x, float y);

// ==========================================
// 📡 CLOUD SIGNAL RECEIVER
// ==========================================
// ব্রাউজার (Firebase) থেকে লগইন সফল হলে এই ফাংশন কল করে ডেটা সেট করা হবে
void SetUserLoginData(std::wstring name, std::wstring email, std::wstring uid);
