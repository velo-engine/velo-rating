// Google App Rating & StoreKit bridge for iOS (Objective-C).
// Supports native In-App Review via SKStoreReviewController and direct App Store links.

#pragma once

#if defined(__APPLE__)
#include <TargetConditionals.h>
#if TARGET_OS_IPHONE || TARGET_OS_SIMULATOR

#import <Foundation/Foundation.h>
#import <UIKit/UIKit.h>
#import <StoreKit/StoreKit.h>

static inline void velo_rating_ios_init(void) {
    // Optional prewarm or initialization
}

static inline int velo_rating_ios_is_available(void) {
    Class reviewCls = NSClassFromString(@"SKStoreReviewController");
    return reviewCls != nil ? 1 : 0;
}

static inline UIWindowScene* velo_rating_ios_find_active_scene(void) {
    if (@available(iOS 13.0, *)) {
        for (UIScene *scene in [UIApplication sharedApplication].connectedScenes) {
            if (scene.activationState == UISceneActivationStateForegroundActive && [scene isKindOfClass:[UIWindowScene class]]) {
                return (UIWindowScene *)scene;
            }
        }
    }
    return nil;
}

static inline int velo_rating_ios_request_review(void) {
    Class reviewCls = NSClassFromString(@"SKStoreReviewController");
    if (!reviewCls) {
        NSLog(@"[VeloRating] SKStoreReviewController not available on this iOS version");
        return 0;
    }

    dispatch_async(dispatch_get_main_queue(), ^{
        if (@available(iOS 14.0, *)) {
            UIWindowScene *scene = velo_rating_ios_find_active_scene();
            if (scene && [reviewCls respondsToSelector:@selector(requestReviewInScene:)]) {
                [reviewCls requestReviewInScene:scene];
                return;
            }
        }

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
        if ([reviewCls respondsToSelector:@selector(requestReview)]) {
            [reviewCls requestReview];
        }
#pragma clang diagnostic pop
    });
    return 1;
}

static inline int velo_rating_ios_open_store(const char* app_id_c, int write_review) {
    if (!app_id_c || strlen(app_id_c) == 0) {
        return 0;
    }
    NSString *raw = [NSString stringWithUTF8String:app_id_c];
    NSString *clean = [raw stringByTrimmingCharactersInSet:[NSCharacterSet whitespaceAndNewlineCharacterSet]];
    if ([clean hasPrefix:@"id"]) {
        clean = [clean substringFromIndex:2];
    }
    if (clean.length == 0) {
        return 0;
    }

    NSString *scheme = write_review
        ? [NSString stringWithFormat:@"itms-apps://itunes.apple.com/app/id%@?action=write-review", clean]
        : [NSString stringWithFormat:@"itms-apps://itunes.apple.com/app/id%@", clean];

    NSString *webFallback = write_review
        ? [NSString stringWithFormat:@"https://apps.apple.com/app/id%@?action=write-review", clean]
        : [NSString stringWithFormat:@"https://apps.apple.com/app/id%@", clean];

    dispatch_async(dispatch_get_main_queue(), ^{
        NSURL *url = [NSURL URLWithString:scheme];
        UIApplication *app = [UIApplication sharedApplication];
        if ([app canOpenURL:url]) {
            if (@available(iOS 10.0, *)) {
                [app openURL:url options:@{} completionHandler:nil];
            } else {
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
                [app openURL:url];
#pragma clang diagnostic pop
            }
        } else {
            NSURL *webUrl = [NSURL URLWithString:webFallback];
            if (@available(iOS 10.0, *)) {
                [app openURL:webUrl options:@{} completionHandler:nil];
            } else {
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
                [app openURL:webUrl];
#pragma clang diagnostic pop
            }
        }
    });
    return 1;
}

#else

static inline void velo_rating_ios_init(void) {}
static inline int velo_rating_ios_is_available(void) { return 0; }
static inline int velo_rating_ios_request_review(void) { return 0; }
static inline int velo_rating_ios_open_store(const char* app_id_c, int write_review) { return 0; }

#endif // TARGET_OS_IPHONE || TARGET_OS_SIMULATOR
#endif // __APPLE__
