
#include "LoginUI.h"
#import <UIKit/UITextField.h>
#import <UIKit/UIGraphics.h>

#define margin 3
#define line 0.6

@implementation LoginUI

- (id)initWithFrame:(CGRect)frame ip:(const char*) ip chan:(uint32_t) chan
{
    CGFloat height = (frame.size.height - 4 * margin - 3 * line) / 2;

    if (self = [super initWithFrame:frame]) {
        self.backgroundColor = [UIColor whiteColor];
        self.userInteractionEnabled = YES;

        frame.origin.y = margin + line;
        frame.size.height = height;
        UITextField* cip = [[UITextField alloc] initWithFrame:frame];

        cip.placeholder = @"127.0.0.1";
        cip.text = [NSString stringWithUTF8String:ip];
        cip.tag = 3;
        cip.keyboardType = UIKeyboardTypeDecimalPad;
        cip.backgroundColor = [UIColor whiteColor];
        [self addSubview:cip];
        [cip autorelease];

        frame.origin.y += frame.size.height + 2 * margin + line;
        UITextField* cport = [[UITextField alloc] initWithFrame:frame];
        cport.keyboardType = UIKeyboardTypeNumberPad;
        cport.backgroundColor = [UIColor whiteColor];

        char s[64];
        sprintf(s, "%d", chan);
        cport.placeholder = @"100000001";
        cport.text = [NSString stringWithUTF8String:s];
        cport.tag = 4;
        [self addSubview:cport];
        [cport autorelease];
    }
    return self;
}

- (void)drawRect:(CGRect)rect
{
    CGRect frame = self.frame;
    CGContextRef context = UIGraphicsGetCurrentContext();
    CGContextSetLineCap(context, kCGLineCapSquare);
    CGContextSetLineWidth(context, line);
    CGContextSetShouldAntialias(context, NO);

    CGFloat y = 0.0;
    CGContextSetRGBStrokeColor(context, 0.7, 0.7, 0.7, 1.0);
    CGContextBeginPath(context);
    CGContextMoveToPoint(context, 0, y);
    CGContextAddLineToPoint(context, frame.size.width, y);

    y = frame.size.height / 2;
    CGContextMoveToPoint(context, 0, y);
    CGContextAddLineToPoint(context, frame.size.width, y);

    y = frame.size.height;
    CGContextMoveToPoint(context, 0, y);
    CGContextAddLineToPoint(context, frame.size.width, y);
    CGContextStrokePath(context);
}

+ (UInt32) prefer_height
{
    return 54;
}

- (BOOL)pointInside:(CGPoint)point withEvent:(UIEvent *)event
{
    for (UIView *subView in self.subviews) {
        if ([subView isFirstResponder]) {
            return YES;
        }
    }
    return [super pointInside:point withEvent:event];
}

- (void)touchesEnded:(NSSet *)touches withEvent:(UIEvent *)event
{
    [self endEditing:YES];
}

- (NSString *) ip
{
    UITextField* ip = (UITextField *) [self viewWithTag:3];
    NSString* s = ip.text;
    return s;
}

- (NSString *) chan
{
    UITextField* chan = (UITextField *) [self viewWithTag:4];
    NSString* s = chan.text;
    return s;
}

@end

