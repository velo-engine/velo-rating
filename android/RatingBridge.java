package io.velo.rating;

import android.app.Activity;
import android.content.ActivityNotFoundException;
import android.content.Intent;
import android.net.Uri;
import android.util.Log;
import java.lang.reflect.InvocationHandler;
import java.lang.reflect.Method;
import java.lang.reflect.Proxy;

public class RatingBridge {
    private static final String TAG = "VeloRating";

    // Result flags: 0 = none, 1 = completed/dismissed, 2 = failed
    private static volatile int sReviewResult = 0;
    private static volatile boolean sIsRequesting = false;

    public static void init(Activity activity) {
        Log.d(TAG, "RatingBridge initialized");
    }

    public static boolean isReviewAvailable(Activity activity) {
        try {
            Class<?> factoryCls = Class.forName("com.google.android.play.core.review.ReviewManagerFactory");
            return factoryCls != null;
        } catch (Throwable t) {
            return false;
        }
    }

    public static int pollReviewResult() {
        int r = sReviewResult;
        sReviewResult = 0;
        return r;
    }

    public static boolean isRequesting() {
        return sIsRequesting;
    }

    public static void requestReview(final Activity activity, final String fallbackPackageName, final boolean fallbackToStore) {
        if (activity == null) {
            Log.e(TAG, "requestReview: Activity is null");
            sReviewResult = 2;
            return;
        }

        activity.runOnUiThread(new Runnable() {
            @Override
            public void run() {
                try {
                    sIsRequesting = true;
                    sReviewResult = 0;

                    // Dynamically invoke Play Core Review API via reflection:
                    Class<?> factoryCls = Class.forName("com.google.android.play.core.review.ReviewManagerFactory");
                    Method createMethod = factoryCls.getMethod("create", android.content.Context.class);
                    final Object manager = createMethod.invoke(null, activity);

                    if (manager == null) {
                        Log.w(TAG, "ReviewManagerFactory returned null");
                        handleFailure(activity, fallbackPackageName, fallbackToStore);
                        return;
                    }

                    Method requestFlowMethod = manager.getClass().getMethod("requestReviewFlow");
                    final Object requestTask = requestFlowMethod.invoke(manager);

                    final Class<?> onCompleteInterface = Class.forName("com.google.android.gms.tasks.OnCompleteListener");
                    final Method addOnCompleteMethod = requestTask.getClass().getMethod("addOnCompleteListener", onCompleteInterface);

                    // Dynamic proxy for OnCompleteListener<ReviewInfo>
                    Object requestListener = Proxy.newProxyInstance(
                        onCompleteInterface.getClassLoader(),
                        new Class<?>[]{onCompleteInterface},
                        new InvocationHandler() {
                            @Override
                            public Object invoke(Object proxy, Method method, Object[] args) throws Throwable {
                                if (method.getName().equals("onComplete") && args != null && args.length > 0) {
                                    Object task = args[0];
                                    Method isSuccessfulMethod = task.getClass().getMethod("isSuccessful");
                                    Boolean successful = (Boolean) isSuccessfulMethod.invoke(task);

                                    if (Boolean.TRUE.equals(successful)) {
                                        Method getResultMethod = task.getClass().getMethod("getResult");
                                        Object reviewInfo = getResultMethod.invoke(task);

                                        Class<?> reviewInfoCls = Class.forName("com.google.android.play.core.review.ReviewInfo");
                                        Method launchMethod = manager.getClass().getMethod("launchReviewFlow", Activity.class, reviewInfoCls);
                                        Object flowTask = launchMethod.invoke(manager, activity, reviewInfo);

                                        // Dynamic proxy for OnCompleteListener<Void>
                                        Object flowListener = Proxy.newProxyInstance(
                                            onCompleteInterface.getClassLoader(),
                                            new Class<?>[]{onCompleteInterface},
                                            new InvocationHandler() {
                                                @Override
                                                public Object invoke(Object flowProxy, Method flowMethod, Object[] flowArgs) throws Throwable {
                                                    if (flowMethod.getName().equals("onComplete")) {
                                                        sIsRequesting = false;
                                                        sReviewResult = 1;
                                                        Log.d(TAG, "In-app review flow completed");
                                                    }
                                                    return null;
                                                }
                                            }
                                        );
                                        addOnCompleteMethod.invoke(flowTask, flowListener);
                                    } else {
                                        Log.w(TAG, "requestReviewFlow task was not successful");
                                        handleFailure(activity, fallbackPackageName, fallbackToStore);
                                    }
                                }
                                return null;
                            }
                        }
                    );
                    addOnCompleteMethod.invoke(requestTask, requestListener);

                } catch (Throwable t) {
                    Log.w(TAG, "Failed to launch in-app review: " + t.getMessage());
                    handleFailure(activity, fallbackPackageName, fallbackToStore);
                }
            }
        });
    }

    private static void handleFailure(Activity activity, String fallbackPackageName, boolean fallbackToStore) {
        sIsRequesting = false;
        sReviewResult = 2;
        if (fallbackToStore) {
            Log.d(TAG, "Falling back to opening Play Store page");
            openStore(activity, fallbackPackageName);
        }
    }

    public static void openStore(final Activity activity, final String packageName) {
        if (activity == null) return;
        activity.runOnUiThread(new Runnable() {
            @Override
            public void run() {
                String pkg = packageName;
                if (pkg == null || pkg.trim().isEmpty()) {
                    pkg = activity.getPackageName();
                }
                Log.d(TAG, "Opening Play Store for: " + pkg);
                try {
                    Uri marketUri = Uri.parse("market://details?id=" + pkg);
                    Intent intent = new Intent(Intent.ACTION_VIEW, marketUri);
                    intent.addFlags(Intent.FLAG_ACTIVITY_NO_HISTORY |
                                    Intent.FLAG_ACTIVITY_NEW_DOCUMENT |
                                    Intent.FLAG_ACTIVITY_MULTIPLE_TASK |
                                    Intent.FLAG_ACTIVITY_NEW_TASK);
                    activity.startActivity(intent);
                } catch (ActivityNotFoundException e) {
                    try {
                        Uri webUri = Uri.parse("https://play.google.com/store/apps/details?id=" + pkg);
                        Intent webIntent = new Intent(Intent.ACTION_VIEW, webUri);
                        webIntent.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
                        activity.startActivity(webIntent);
                    } catch (Throwable t) {
                        Log.e(TAG, "Failed to open Play Store via web url", t);
                    }
                }
            }
        });
    }
}
