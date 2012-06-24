#include "testApp.h"


struct FlowPoint{
    ofVec3f pos;
    ofVec3f color;
};


//軌跡を描画するためのクラス
class FlowLine{
    
public:
    ofVec3f pos;//軌跡の先頭位置
    ofVec3f color;//軌跡の先頭の色
    deque<FlowPoint> points;//軌跡を構成する点列
    float alpha;//アルファ値
    float rise_speed;//上昇スピード
    FlowLine(){
        //軌跡が登っていくスピードをランダムで散らす
        rise_speed=ofRandom(0.5,5);
    }
    void update(){
        //軌跡の先頭に現在のポイントを追加
        FlowPoint p;
        //平滑化してる　2割ぐらい一つ前の値をいれるかな？
        p.pos=(points.front().pos*0.2+pos*0.8);
        
        p.color=(points.front().color*0.2+color*0.8);
        
        points.push_front(p);
        if (points.size()>2){
            for(int i=1;i<points.size();i++){
                //軌跡がだんだん登って行くように
                points[i].pos.y -= rise_speed;
                
                // 線をなめらかに
                //この平滑化をコメントアウトすると、線分が細切れになる
                //points[i-i].pos = points[i].pos*0.6+points[i-1].pos*0.4;
                
            }
        }
        //軌跡の長さを制限する
        if(points.size()>100)
            points.pop_back();
        //アルファ値をだんだん小さくする
        alpha += -alpha *0.05;
    }
    
    void drow(){
        //軌跡の描画　//OpenGLの関数で描画
        glBegin(GL_LINE_STRIP);

        for (int i=0; i<points.size(); i++) {
            FlowPoint &p =points[i];
            float a=1- (float)i / (float)points.size();//だんだん透明に
            glColor4f(p.color.x, p.color.y, p.color.z, a*alpha);
            glVertex2f(p.pos.x,p.pos.y); //(p.pos.v);
            
        }
        glEnd();        
    }
    
    //アルファ値が十分に小さいときに消去するためのフラグ用の関数
    bool alive(){
        return alpha > 0.05;
    }
};

//軌跡オブジェクトの配列
vector<FlowLine*> flow_lines;

//一番近い点を持ったオブジェクトを探すためにソートをかける関数オブジェクト
struct sort_by_distance {
    sort_by_distance(ofVec2f pos){
        this->pos =pos;
    }
    bool operator()(const FlowLine* a, const FlowLine* b){
        float len_a = (a->pos - pos).squareLength();
        float len_b = (b->pos - pos).squareLength();
        return len_a < len_b;
    }
    
    ofVec2f pos;
};

//検出したフローに対する処理
void testApp::updateFlowPoint(ofVec2f to, ofVec2f from){
    //始点と終点のベクトル距離を求める
    float len=(from -to).length();
    
    //距離がいい感じだったら
    if(len > 1 && len <50){
        //終点周辺のピクセルの色を取得
        CvScalar c =cvGet2D(colorImg.getCvImage() , (int)to.y, (int)to.x );
        
        //周囲の色を取得する
        ofVec4f color = ofVec4f(c.val[0]/255.0f, c.val[1]/255.0f);
        
        
        //temp_linesの中のオブジェクトを始点に近い順にソート
        sort(flow_lines.begin(), flow_lines.end(), sort_by_distance(from));
        
        if(flow_lines.empty()||(flow_lines[0]->pos -from ).length()>30){
            //temp_linesがからか、支店の近くにオブジェクトがない場合新しく追加
            FlowLine *line = new FlowLine();
            line->pos = to;
            line->alpha = 0;
            line->color = color;
            FlowPoint point;
            point.pos = to;
            point.color=color;
            line->points.push_back(point);
            flow_lines.push_back(line);
            
        }
        else{
            //近いオブジェクトが見つかったので色や位置を更新
            FlowLine *line = flow_lines[0];
            line->color = color;
            line->pos = to;
            line->alpha += (1 - line->alpha) * 0.1;
            
        }
    }
}

//--------------------------------------------------------------
void testApp::setup(){
ofSetFrameRate(12); 
	#ifdef _USE_LIVE_VIDEO
        vidGrabber.setVerbose(true);
        vidGrabber.initGrabber(320,240);
	#else
        vidPlayer.loadMovie("fingers.mov");
        vidPlayer.play();
	#endif

    colorImg.allocate(320,240);
	grayImage.allocate(320,240);
	grayBg.allocate(320,240);
	grayDiff.allocate(320,240);
    
    //追加
	prevGrayImage.allocate(320,240);
    
	bLearnBakground = true;
	threshold = 80;
}

//--------------------------------------------------------------
void testApp::update(){
	ofBackground(100,100,100);
    
	#ifdef _USE_LIVE_VIDEO
       vidGrabber.grabFrame();
	   bNewFrame = vidGrabber.isFrameNew();
    #else
        vidPlayer.idleMovie();
        bNewFrame = vidPlayer.isFrameNew();
	#endif

	if (bNewFrame){

		#ifdef _USE_LIVE_VIDEO
           colorImg.setFromPixels(vidGrabber.getPixels(), 320,240);
           
	    #else
           colorImg.setFromPixels(vidPlayer.getPixels(), 320,240);
        #endif
        
        
        //オプティカルフロー用のサイズ
        CvSize VIDEO_SIZE=cvSize(320.0,240.0);
        
        int count=150;//検出するポイントの最大数
                
        static IplImage *eig=cvCreateImage(VIDEO_SIZE, IPL_DEPTH_32F, 1);
        static IplImage *temp=cvCreateImage(VIDEO_SIZE, IPL_DEPTH_32F, 1);
        
        static CvPoint2D32f *corners1=(CvPoint2D32f*)cvAlloc(count *sizeof(CvPoint2D32f));
        static CvPoint2D32f *corners2=(CvPoint2D32f*)cvAlloc(count *sizeof(CvPoint2D32f));
        
        static IplImage *prev_pyramid = cvCreateImage(cvSize(320.0+8, 240.0 / 3), IPL_DEPTH_8U, 1);
        static IplImage *curr_pyramid = cvCreateImage(cvSize(320.0+8, 240.0 / 3), IPL_DEPTH_8U, 1);
        
        static char *status =(char*)cvAlloc(count);
        
        //src グレイスケールを変換しつつ；・・

        //これでグレースケールに変換する
        grayImage = colorImg;
      
        // v0060のコードだとグレースケールは以下
        // cvCvtColor(colorImg.getCvImage(),grayImage.getCvImage(), CV_RGB2BGR);
        
        //特徴点を抽出
        float block_size= 10;//検出するポイント間の最短距離
    
        //　コーナー検出
        cvGoodFeaturesToTrack(grayImage.getCvImage(), eig, temp, corners1, &count, 0.001, block_size);
        
        // v0060のコードのコーナー検出
        //cvGoodFeaturesToTrack(&grayImage, eig, temp, corners1, &count, 0.001, block_size,NULL);
        
        
        //grayImageとprevGrayImageについてオプティカルフロー
        cvCalcOpticalFlowPyrLK(grayImage.getCvImage(), prevGrayImage.getCvImage(), curr_pyramid, prev_pyramid, corners1, corners2, count, cvSize(10, 10), 4, status, NULL, cvTermCriteria(CV_TERMCRIT_EPS|CV_TERMCRIT_ITER, 64, 0.01), 0);

        // v0060のコードのオプティカルフロー
        //cvCalcOpticalFlowPyrLK(grayImage, prevGrayImage, curr_pyramid, prev_pyramid, corners1, corners2, count, cvSize(10, 10), 4, status, NULL, cvTermCriteria(CV_TERMCRIT_EPS|CV_TERMCRIT_ITER, 64, 0.01), 0);
        
    
        //grayImageをprevGrayImageにコピー
        prevGrayImage=grayImage;
        
        
        //検出できたフローに対するループ
        for (int i=0; i<count; i++) {
            if(status[i]){
                //始点
                ofVec2f to =ofVec2f(corners1[i].x,corners1[i].y);
                //終点
                ofVec2f from =ofVec2f(corners2[i].x,corners2[i].y);
                //取得したフローで何かしらの処理をする
                updateFlowPoint(to,from);                
            }
        }
        
        //FlowLineの更新　削除
        vector<FlowLine*>::iterator it= flow_lines.begin();
        while (it != flow_lines.end()) {
            FlowLine *line = *it;
            //更新
            line->update();
            
            //オブジェクトの存在チェック
            if(line->alive() == false){
                it =flow_lines.erase(it);
                delete line;
            }else{
                it++;
            }
        }
        
        
        ///////////////////////////////////////
		if (bLearnBakground == true){
			grayBg = grayImage;		// the = sign copys the pixels from grayImage into grayBg (operator overloading)
			bLearnBakground = false;
		}

		// take the abs value of the difference between background and incoming and then threshold:
		grayDiff.absDiff(grayBg, grayImage);
		grayDiff.threshold(threshold);

		// find contours which are between the size of 20 pixels and 1/3 the w*h pixels.
		// also, find holes is set to true so we will get interior contours as well....
		contourFinder.findContours(grayDiff, 20, (340*240)/3, 10, true);	// find holes
	}

}


//--------------------------------------------------------------
void testApp::draw(){

	// draw the incoming, the grayscale, the bg and the thresholded difference
	ofSetHexColor(0xffffff);
	colorImg.draw(20,20);
	grayImage.draw(360,20);
	grayBg.draw(20,280);
	grayDiff.draw(360,280);

	// then draw the contours:

	ofFill();
	ofSetHexColor(0x333333);
	ofRect(360,540,320,240);
	ofSetHexColor(0xffffff);

	// we could draw the whole contour finder
	//contourFinder.draw(360,540);

	// or, instead we can draw each blob individually,
	// this is how to get access to them:
    for (int i = 0; i < contourFinder.nBlobs; i++){
        contourFinder.blobs[i].draw(360,540);
    }

    
    //FlowLineの描画
    vector<FlowLine*>::iterator it= flow_lines.begin();
    while (it != flow_lines.end()) {
        FlowLine *line = *it;
        //更新
        line->drow();
        it++;
        
    }

    
	// finally, a report:

	ofSetHexColor(0xffffff);
	char reportStr[1024];
   sprintf(reportStr, "bg subtraction and blob detection\npress ' ' to capture bg\nthreshold %i (press: +/-)\nnum blobs found %i, fps: %f", threshold, contourFinder.nBlobs, ofGetFrameRate());
	ofDrawBitmapString(reportStr, 20, 600);
}

//--------------------------------------------------------------
void testApp::keyPressed(int key){

	switch (key){
		case ' ':
			bLearnBakground = true;
			break;
		case '+':
			threshold ++;
			if (threshold > 255) threshold = 255;
			break;
		case '-':
			threshold --;
			if (threshold < 0) threshold = 0;
			break;
	}
}

//--------------------------------------------------------------
void testApp::keyReleased(int key){

}

//--------------------------------------------------------------
void testApp::mouseMoved(int x, int y ){

}

//--------------------------------------------------------------
void testApp::mouseDragged(int x, int y, int button){

}

//--------------------------------------------------------------
void testApp::mousePressed(int x, int y, int button){

}

//--------------------------------------------------------------
void testApp::mouseReleased(int x, int y, int button){

}

//--------------------------------------------------------------
void testApp::windowResized(int w, int h){

}

//--------------------------------------------------------------
void testApp::gotMessage(ofMessage msg){

}

//--------------------------------------------------------------
void testApp::dragEvent(ofDragInfo dragInfo){ 

}
