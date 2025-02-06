
#import <UIKit/UIKit.h>
#import <UIKit/UIColor.h>
#include "area.h"

@interface GLView : UIView {
@private
    void* egl;
@public
    float x, y, w, h;
}

- (id) initWithFrame:(CGRect)frame r:(float)r g:(float)g b:(float)b a:(float)a;
- (void) suspend:(intptr_t) yes;
- (void *) egl_get;
- (UIImage *) snap;

@end
