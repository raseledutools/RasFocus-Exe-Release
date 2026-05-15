#pragma once

#include <string>

class FamilyLink {
public:
    FamilyLink();
    ~FamilyLink();

    // ইউজারের লগইন এবং সাবস্ক্রিপশন প্যাকেজ চেক করার মেইন ফাংশন
    std::string CheckFamilyLinkStatus(const std::string& userEmail, bool hasComboPackage);

    // বাচ্চার পিসি থেকে দেওয়া ৬-ডিজিটের পিন ভেরিফাই করার ফাংশন
    std::string ProcessPairingPin(const std::string& pin);

private:
    bool isConnected;
    std::string connectedParentUid;
};
