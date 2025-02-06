//
//  LoginUI_UIView.h
//  media
//
//  Created by zylthinking on 15-3-10.
//  Copyright (c) 2015年 qihoo. All rights reserved.
//

#import <UIKit/UIView.h>

@interface LoginUI : UIView
- (id)initWithFrame:(CGRect)frame ip:(const char*) ip chan:(uint32_t) chan;
+ (UInt32) prefer_height;
- (NSString *) ip;
- (NSString *) chan;

@end
