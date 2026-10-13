package org.dc4engine;

import android.app.AlertDialog;
import android.content.Context;
import android.content.res.ColorStateList;
import android.graphics.*;
import android.graphics.drawable.*;
import android.view.*;
import android.widget.*;

/** The adv32 / Circus Engine visual system for Arisa32. */
final class ArisaUi {
    static final int INK=0xff0c121c, PANEL=0xf5131b26, TEXT=0xffe6edf5;
    static final int MUTED=0xff8899ac, CYAN=0xff38bdf8, GOLD=0xfff59e0b, LINE=0x33455a73;
    private static Bitmap wallpaper;
    static int dp(Context c,float n){return (int)(n*c.getResources().getDisplayMetrics().density+.5f);}
    static GradientDrawable shape(Context c,int color,int radius){
        GradientDrawable d=new GradientDrawable();d.setColor(color);d.setCornerRadius(dp(c,radius));return d;
    }
    static Drawable surface(Context c,int color,int radius){
        GradientDrawable d=shape(c,color,radius);d.setStroke(dp(c,1),LINE);return d;
    }
    static Drawable ripple(Context c,int color,int radius){
        return new RippleDrawable(ColorStateList.valueOf(0x2838bdf8),surface(c,color,radius),shape(c,Color.WHITE,radius));
    }
    static TextView text(Context c,String label,int size,int color){
        TextView t=new TextView(c);t.setText(label);t.setTextSize(size);t.setTextColor(color);
        t.setFontFeatureSettings("kern");t.setIncludeFontPadding(false);t.setLineSpacing(dp(c,2),1);
        return t;
    }
    static TextView eyebrow(Context c,String label){
        TextView t=text(c,label,10,CYAN);t.setLetterSpacing(.12f);t.setTypeface(Typeface.create("sans-serif-bold",Typeface.BOLD));return t;
    }
    static Button button(Context c,String label,boolean primary,Runnable action){
        Button b=new Button(c);b.setText(label);b.setAllCaps(true);b.setTextSize(13);
        b.setTypeface(Typeface.create("sans-serif-bold",Typeface.BOLD));b.setTextColor(primary?INK:TEXT);
        b.setBackground(ripple(c,primary?CYAN:0xd01c2738,6));b.setMinHeight(dp(c,44));
        b.setMinimumHeight(dp(c,44));b.setMinWidth(0);b.setMinimumWidth(0);
        b.setPadding(dp(c,16),dp(c,8),dp(c,16),dp(c,8));b.setStateListAnimator(null);
        b.setOnClickListener(v->action.run());return b;
    }
    static EditText input(Context c,String hint){
        EditText e=new EditText(c);e.setTextColor(TEXT);e.setHintTextColor(MUTED);e.setTextSize(14);
        e.setSingleLine();e.setHint(hint);e.setSelectAllOnFocus(true);
        e.setBackground(surface(c,0xff0e1622,6));e.setPadding(dp(c,14),dp(c,12),dp(c,14),dp(c,12));
        return e;
    }
    static View divider(Context c){View v=new View(c);v.setBackgroundColor(LINE);v.setLayoutParams(new LinearLayout.LayoutParams(-1,dp(c,1)));return v;}
    static LinearLayout column(Context c){LinearLayout v=new LinearLayout(c);v.setOrientation(LinearLayout.VERTICAL);return v;}
    static void gap(LinearLayout parent,int dp){parent.addView(new View(parent.getContext()),new LinearLayout.LayoutParams(1,ArisaUi.dp(parent.getContext(),dp)));}
    static TextView badge(Context c,String label){
        TextView t=text(c,label,10,CYAN);t.setBackground(surface(c,0xdd101824,4));
        t.setTypeface(Typeface.create("sans-serif-bold",Typeface.BOLD));
        t.setPadding(dp(c,8),dp(c,4),dp(c,8),dp(c,4));return t;
    }
    static TextView goldBadge(Context c,String label){
        TextView t=text(c,label,10,GOLD);t.setBackground(surface(c,0xdd221c10,4));
        t.setTypeface(Typeface.create("sans-serif-bold",Typeface.BOLD));
        t.setPadding(dp(c,8),dp(c,4),dp(c,8),dp(c,4));return t;
    }
    static ImageView logo(Context c){ImageView v=new ImageView(c);v.setImageResource(R.drawable.arisa_mark);v.setImportantForAccessibility(View.IMPORTANT_FOR_ACCESSIBILITY_NO);return v;}

    static final class Wallpaper extends View {
        private final Paint paint=new Paint(Paint.ANTI_ALIAS_FLAG|Paint.FILTER_BITMAP_FLAG);
        private final RectF destination=new RectF();
        private Shader shade;
        Wallpaper(Context c){
            super(c);setImportantForAccessibility(IMPORTANT_FOR_ACCESSIBILITY_NO);
            if(wallpaper==null){BitmapFactory.Options o=new BitmapFactory.Options();o.inPreferredConfig=Bitmap.Config.RGB_565;
                wallpaper=BitmapFactory.decodeResource(c.getResources(),R.drawable.arisa_wallpaper,o);}
        }
        @Override protected void onSizeChanged(int w,int h,int ow,int oh){
            if(wallpaper!=null){float scale=Math.max(w/(float)wallpaper.getWidth(),h/(float)wallpaper.getHeight());
                float bw=wallpaper.getWidth()*scale,bh=wallpaper.getHeight()*scale;
                float x=(w-bw)*.68f,y=(h-bh)*.40f;destination.set(x,y,x+bw,y+bh);}
            shade=new LinearGradient(0,0,w,0,new int[]{0xfc0c121c,0xee0c121c,0x990c121c,0x550c121c},new float[]{0,.36f,.68f,1},Shader.TileMode.CLAMP);
        }
        @Override protected void onDraw(Canvas c){
            c.drawColor(INK);paint.setShader(null);if(wallpaper!=null)c.drawBitmap(wallpaper,null,destination,paint);
            paint.setShader(shade);c.drawRect(0,0,getWidth(),getHeight(),paint);paint.setShader(null);
            paint.setColor(0x180c121c);c.drawRect(0,0,getWidth(),getHeight(),paint);
        }
    }

    static FrameLayout backdrop(Context c){
        FrameLayout root=new FrameLayout(c);root.addView(new Wallpaper(c),new FrameLayout.LayoutParams(-1,-1));return root;
    }

    /** Keep custom dialog content scrollable even on short landscape windows. */
    static final class LimitedScroll extends ScrollView {
        LimitedScroll(Context c){super(c);setClipToPadding(false);}
        @Override protected void onMeasure(int widthSpec,int heightSpec){
            int available=getResources().getDisplayMetrics().heightPixels-dp(getContext(),160);
            int limit=Math.max(dp(getContext(),100),Math.min(dp(getContext(),420),available));
            if(MeasureSpec.getMode(heightSpec)!=MeasureSpec.UNSPECIFIED)limit=Math.min(limit,MeasureSpec.getSize(heightSpec));
            super.onMeasure(widthSpec,MeasureSpec.makeMeasureSpec(limit,MeasureSpec.AT_MOST));
        }
    }

    static final class DialogBuilder extends AlertDialog.Builder {
        private final Context context;
        private CharSequence title;
        DialogBuilder(Context c){super(c,R.style.ArisaDialog);context=c;}
        @Override public AlertDialog.Builder setTitle(CharSequence value){title=value;return this;}
        @Override public AlertDialog create(){
            LinearLayout heading=column(context);heading.setPadding(dp(context,20),dp(context,18),dp(context,20),dp(context,10));
            heading.addView(eyebrow(context,"ADV32 / CIRCUS ENGINE"));gap(heading,6);
            TextView caption=text(context,title==null?"Arisa32":title.toString(),20,TEXT);
            caption.setMaxLines(2);caption.setEllipsize(android.text.TextUtils.TruncateAt.END);
            caption.setTypeface(Typeface.create("sans-serif-bold",Typeface.BOLD));heading.addView(caption);
            setCustomTitle(heading);
            AlertDialog d=super.create();
            d.setOnShowListener(ignored->{
                Window window=d.getWindow();if(window!=null){
                    int width=context.getResources().getDisplayMetrics().widthPixels;
                    window.setBackgroundDrawable(surface(context,0xff131b26,12));
                    window.setLayout(Math.min(dp(context,540),width-dp(context,40)),-2);
                    window.setDimAmount(.55f);
                }
                for(int which:new int[]{AlertDialog.BUTTON_POSITIVE,AlertDialog.BUTTON_NEGATIVE,AlertDialog.BUTTON_NEUTRAL}){
                    Button b=d.getButton(which);if(b!=null){b.setAllCaps(true);b.setTextColor(CYAN);b.setTextSize(13);b.setMinHeight(dp(context,44));}
                }
                TextView message=d.findViewById(android.R.id.message);if(message!=null){message.setTextColor(MUTED);message.setTextSize(14);message.setLineSpacing(dp(context,3),1);}
                if(d.getListView()!=null){d.getListView().setDivider(new ColorDrawable(LINE));d.getListView().setDividerHeight(dp(context,1));}
            });return d;
        }
        @Override public AlertDialog show(){AlertDialog d=create();d.show();return d;}
    }
}
