
#import "tableController.h"
#import "LoginUI.h"
#import <QuartzCore/CAAnimation.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <time.h>

@implementation tableController

- (id)initWithStyle:(UITableViewStyle)style frame:(CGRect)frame;
{
    srand((unsigned) time(NULL));
    if (self = [super initWithStyle:style]) {
        strcpy(self->ip, "103.28.8.110");
        self->port = 80;
        self->uid = rand();
        self->chan = 100000008;
        self->run = 0;

        UITableView* table = [[UITableView alloc] initWithFrame:frame style:style];
        self.view = table;
        table.delegate = self;
        table.dataSource = self;
        table.backgroundView = nil;
        table.backgroundColor = [UIColor whiteColor];
        table.scrollEnabled = NO;
    }
    return self;
}

- (void)didReceiveMemoryWarning
{
    [super didReceiveMemoryWarning];
}

#pragma mark - Table view data source
- (NSInteger)numberOfSectionsInTableView:(UITableView *)tableView
{
    return 1;
}

- (NSInteger)tableView:(UITableView *)tableView numberOfRowsInSection:(NSInteger)section
{
    return 2;
}

- (UITableViewCell *)tableView:(UITableView *)tableView cellForRowAtIndexPath:(NSIndexPath *)indexPath
{
    char str[256];
    NSInteger row = indexPath.row;
    UITableViewCell* cell = [[[UITableViewCell alloc] initWithStyle:UITableViewCellStyleDefault reuseIdentifier:nil] autorelease];
    cell.selectionStyle = UITableViewCellSelectionStyleNone;
    cell.backgroundColor = [UIColor whiteColor];

    if (row == 0) {
        sprintf(str, "room: %d@%s", self->chan, self->ip);
        cell.accessoryType = UITableViewCellAccessoryDisclosureIndicator;
    } else {
        sprintf(str, "uid: %d", self->uid);
        UISwitch* sb = [[UISwitch alloc] initWithFrame:CGRectMake(0, 0, 0, 0)];
        cell.accessoryView = sb;
        sb.on = (run == 1) ? YES : NO;
        [sb release];
    }

    NSString* ns = [[[NSString alloc] initWithBytes:str length:strlen(str) encoding:NSASCIIStringEncoding] autorelease];
    cell.textLabel.text = ns;
    return cell;
}

- (void) listen:(id)who action:(SEL)f
{
    UITableView* tab = (UITableView *) self.view;
    NSIndexPath *indexPath = [NSIndexPath indexPathForRow:1 inSection:0];
    UITableViewCell* cell = [tab cellForRowAtIndexPath:indexPath];
    UISwitch* sb = (UISwitch *) cell.accessoryView;
    [sb addTarget:who action:f forControlEvents:UIControlEventValueChanged];
}

- (void) notifyFailed
{
    UITableView* tab = (UITableView *) self.view;
    NSIndexPath *indexPath = [NSIndexPath indexPathForRow:1 inSection:0];
    UITableViewCell* cell = [tab cellForRowAtIndexPath:indexPath];
    UISwitch* sb = (UISwitch *) cell.accessoryView;
    sb.on = NO;
}

#pragma mark - Table view delegate

- (void)tableView:(UITableView *)tableView didSelectRowAtIndexPath:(NSIndexPath *)indexPath
{
    NSInteger row = indexPath.row;
    if (row == 0) {
        CGRect rect = [[UIScreen mainScreen] bounds];
        UIView *myView = [[[UIView alloc] initWithFrame:rect] autorelease];
        myView.backgroundColor = [UIColor colorWithRed:0.9 green:0.9 blue:0.9 alpha:1.0];

        rect.origin.y = 100;
        rect.size.height = [LoginUI prefer_height];
        LoginUI* ui = [(LoginUI *) [[LoginUI alloc] initWithFrame:rect ip:self->ip chan:self->chan] autorelease];
        ui.tag = 1;
        [myView addSubview:ui];

        UIViewController* vc = [[[UIViewController alloc] init] autorelease];
        vc.view = myView;

        UILabel *label = [[[UILabel alloc] initWithFrame:CGRectMake(0, 0, 320, 44)] autorelease];
        label.backgroundColor = [UIColor clearColor];
        label.shadowColor = nil;
        label.textAlignment = NSTextAlignmentCenter;
        label.textColor = [UIColor blackColor];
        label.text= @"修改";

        UIBarButtonItem* leftButton = [[[UIBarButtonItem alloc] initWithTitle:@"<"
                                                                       style:UIBarButtonItemStylePlain
                                                                      target:self
                                                                      action:@selector(cancel)] autorelease];

        UIBarButtonItem* rightButton = [[[UIBarButtonItem alloc] initWithTitle:@"确定"
                                                                        style:UIBarButtonItemStylePlain
                                                                       target:self
                                                                       action:@selector(done)] autorelease];
        vc.navigationItem.titleView = label;
        vc.navigationItem.leftBarButtonItem = leftButton;
        vc.navigationItem.rightBarButtonItem = rightButton;


        UINavigationController* nav = [[[UINavigationController alloc] initWithRootViewController:vc] autorelease];
        //vc.navigationController.navigationBar.tintColor = [UIColor whiteColor];
        nav.modalTransitionStyle = UIModalTransitionStyleFlipHorizontal;
        [self presentViewController:nav animated:YES completion:nil];
    }
}

- (void)cancel
{
    UINavigationController* nav = (UINavigationController *) self.presentedViewController;
    [nav dismissViewControllerAnimated:YES completion:nil];
}

static int verify(const char* str)
{
    struct in_addr addr;
    int ret = inet_pton(AF_INET, str, &addr);
    return (ret > 0);
}

- (void)done
{
    UINavigationController* nav = (UINavigationController *) self.presentedViewController;
    UIViewController* myView = nav.topViewController;
    LoginUI* ui = (LoginUI *) [myView.view viewWithTag:1];

    NSString* nip = [ui ip];
    NSString* nchan = [ui chan];

    const char* sip = [nip UTF8String];
    if (ip == NULL || 0 == verify(sip)) {
        return;
    }

    const char* schan = [nchan UTF8String];
    if (schan == NULL) {
        return;
    }

    uint32_t n = atoi(schan);
    if (n < 100000001) {
        return;
    }
    strcpy(self->ip, sip);
    self->chan = n;

    UITableView* tab = (UITableView *) self.view;
    [tab reloadData];
    [nav dismissViewControllerAnimated:YES completion:nil];
}

@end
