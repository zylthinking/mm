
#include "mydef.h"
#import "AppDelegate.h"
#import "FirstViewController.h"

@implementation AppDelegate

- (void)dealloc
{
    [_window release];
    [super dealloc];
}

- (BOOL)application:(UIApplication *)application didFinishLaunchingWithOptions:(NSDictionary *)launchOptions
{
    application.idleTimerDisabled = YES;
    UITabBarController* tabar = (UITabBarController *) self.window.rootViewController;
    UIView *myView = [[[UIView alloc] initWithFrame:self.window.bounds] autorelease];
    myView.backgroundColor = [UIColor whiteColor];

    UIViewController* myController = [[[UIViewController alloc] init] autorelease];
    myController.view = myView;

    UIBarButtonItem *leftButton = [[[UIBarButtonItem alloc] initWithTitle:@"Cancel"
                                                                   style:UIBarButtonItemStyleBordered
                                                                  target:self
                                                                  action:@selector(cancel)] autorelease];
    myController.navigationItem.leftBarButtonItem = leftButton;

    UINavigationController* myNav = [[[UINavigationController alloc] initWithRootViewController:myController] autorelease];
    myNav.tabBarItem.title = @"nav.tab.title";
    myNav.tabBarItem.image = [UIImage imageNamed:@"first"];

    NSMutableArray* newArray = [NSMutableArray arrayWithArray:tabar.viewControllers];
    [newArray addObject:myNav];
    [tabar setViewControllers:newArray animated:YES];
    [self.window makeKeyAndVisible];

    return YES;
}

-(void) cancel
{
    UITabBarController* tabar = (UITabBarController *) self.window.rootViewController;
    FirstViewController* controller = (FirstViewController *) [tabar.viewControllers objectAtIndex:0];
    [controller change_area_test];
}

- (void)applicationWillResignActive:(UIApplication *)application
{
    UITabBarController* tabar = (UITabBarController *) self.window.rootViewController;
    FirstViewController* controller = (FirstViewController *) [tabar.viewControllers objectAtIndex:0];
    [controller suspend:1];
}

- (void)applicationDidBecomeActive:(UIApplication *)application
{
    UITabBarController* tabar = (UITabBarController *) self.window.rootViewController;
    FirstViewController* controller = (FirstViewController *) [tabar.viewControllers objectAtIndex:0];
    [controller suspend:0];
}

@end
