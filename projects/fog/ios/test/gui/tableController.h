
#import <UIKit/UIKit.h>

@interface tableController : UITableViewController {
    @public
    char ip[64];
    uint32_t port;
    uint32_t uid;
    uint32_t chan;
    uint32_t run;
}

- (id)initWithStyle:(UITableViewStyle)style frame:(CGRect)rect;
- (void) listen:(id)who action:(SEL)f;
- (void) notifyFailed;

@end
