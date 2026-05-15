#include "family_link.h"

FamilyLink::FamilyLink() {
    isConnected = false;
    connectedParentUid = "";
}

FamilyLink::~FamilyLink() {}

// 🟢 কন্ডিশন চেক: ইমেইল লগইন আছে কি না এবং প্যাকেজ কেনা আছে কি না
std::string FamilyLink::CheckFamilyLinkStatus(const std::string& userEmail, bool hasComboPackage) {
    
    // কন্ডিশন ১: যদি কোনো ইমেইল লগইন করা না থাকে
    if (userEmail.empty() || userEmail == "") {
        return "{\"status\": \"error\", \"message\": \"অনুগ্রহ করে প্রথমে আপনার ইমেইল দিয়ে লগইন করুন।\"}";
    }

    // কন্ডিশন ২: যদি ইউজারের 'Combo Package' না থাকে
    if (!hasComboPackage) {
        return "{\"status\": \"upgrade_required\", \"message\": \"প্যারেন্টাল কন্ট্রোল একটি প্রিমিয়াম ফিচার। এটি চালু করতে 'Parental Combo Package' কিনুন এবং আপনার ফোনে RasFocus+ Android APK ডাউনলোড করুন।\"}";
    }

    // কন্ডিশন ৩: যদি আগে থেকেই কানেক্টেড থাকে
    if (isConnected) {
        return "{\"status\": \"connected\", \"message\": \"আপনার পিসিটি সফলভাবে প্যারেন্ট ডিভাইসের সাথে যুক্ত আছে।\"}";
    }

    // কন্ডিশন ৪: সবকিছু ঠিক থাকলে পিন বসানোর অনুমতি দেবে
    return "{\"status\": \"ready\", \"message\": \"অ্যাকাউন্ট ভেরিফায়েড। ফোন অ্যাপ থেকে পাওয়া ৬-ডিজিটের পিন কোডটি প্রবেশ করান।\"}";
}

// 🟢 ৬-ডিজিটের পিন ভেরিফাই করার ব্যাকএন্ড লজিক
std::string FamilyLink::ProcessPairingPin(const std::string& pin) {
    
    // পিন অবশ্যই ৬ ডিজিটের হতে হবে
    if (pin.length() != 6) {
        return "{\"status\": \"invalid_pin\", \"message\": \"সঠিক ৬-ডিজিটের পিন কোড দিন।\"}";
    }

    // TODO: ফায়ারবেস API কল করে পিন ভেরিফাই করার আসল লজিক এখানে বসবে
    // আপাতত একটি ডেমো পিন (123456) দিয়ে চেক করা হচ্ছে
    if (pin == "123456") {
        isConnected = true;
        connectedParentUid = "PARENT_UID_FROM_FIREBASE";
        return "{\"status\": \"success\", \"message\": \"সফলভাবে সংযুক্ত হয়েছে! এখন থেকে এই পিসিটি প্যারেন্ট ডিভাইস থেকে কন্ট্রোল করা যাবে।\"}";
    } else {
        return "{\"status\": \"failed\", \"message\": \"পিন কোডটি ভুল অথবা মেয়াদ শেষ হয়ে গেছে। আবার চেষ্টা করুন।\"}";
    }
}
