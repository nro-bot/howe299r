/* 03 April 2018
 * Edited by nrw
 * Additions: Press any key to simultaneously write data to file and capture image to file
 * Maintains stream of data to terminal
 * Ctrl-C must now be used to exit program
 */
/* @file april_tags.cpp
 * @brief Example application for April tags library
 * @author: Michael Kaess
 *
 * Opens the first available camera (typically a built in camera in a
 * laptop) and continuously detects April tags in the incoming
 * images. Detections are both visualized in the live image and shown
 * in the text console. Optionally allows selecting of a specific
 * camera in case multiple ones are present and specifying image
 * resolution as long as supported by the camera. Also includes the
 * option to send tag detections via a serial port, for example when
 * running on a Raspberry Pi that is connected to an Arduino.
 */
/*
 * use q to close
 * c to write data to ./example.txt
 */

using namespace std;

#include <iostream>
#include <cstring>
#include <vector>
#include <list>
#include <sys/time.h>
#include <iomanip>
#include <algorithm> //remove newline
#include <fstream>
using namespace std;
int flag = 0;

const string usage = "\n"
  "Usage:\n"
  "  apriltags_demo [OPTION...] [IMG1 [IMG2...]]\n"
  "\n"
  "Options:\n"
  "  -h  -?          Show help options\n"
  "  -a              Arduino (send tag ids over serial port)\n"
  "  -d              Disable graphics\n"
  "  -t              Timing of tag extraction\n"
  "  -C <bbxhh>      Tag family (default 36h11)\n"
  "  -D <id>         Video device ID (if multiple cameras present)\n"
  "  -F <fx>         Focal length in pixels\n"
  "  -W <width>      Image width (default 640, availability depends on camera)\n"
  "  -H <height>     Image height (default 480, availability depends on camera)\n"
  "  -S <size>       Tag size (square black frame) in meters\n"
  "  -E <exposure>   Manually set camera exposure (default auto; range 0-10000)\n"
  "  -G <gain>       Manually set camera gain (default auto; range 0-255)\n"
  "  -B <brightness> Manually set the camera brightness (default 128; range 0-255)\n"
  "\n";

const string intro = "\n"
    "April tags test code\n"
    "(C) 2012-2014 Massachusetts Institute of Technology\n"
    "Michael Kaess\n"
    "\n";


#ifndef __APPLE__
#define EXPOSURE_CONTROL // only works in Linux
#endif

#ifdef EXPOSURE_CONTROL
#include <libv4l2.h>
#include <linux/videodev2.h>
#include <fcntl.h>
#include <errno.h>
#endif

// OpenCV library for easy access to USB camera and drawing of images
// on screen
#include "opencv2/opencv.hpp"

// April tags detector and various families that can be selected by command line option
#include "AprilTags/TagDetector.h"
#include "AprilTags/Tag16h5.h"
#include "AprilTags/Tag25h7.h"
#include "AprilTags/Tag25h9.h"
#include "AprilTags/Tag36h9.h"
#include "AprilTags/Tag36h11.h"


// Needed for getopt / command line options processing
#include <unistd.h>
extern int optind;
extern char *optarg;

// For Arduino: locally defined serial port access class
#include "Serial.h"


const char* windowName = "apriltags_demo";


// utility function to provide current system time (used below in
// determining frame rate at which images are being processed)
double tic() {
  struct timeval t;
  gettimeofday(&t, NULL);
  return ((double)t.tv_sec + ((double)t.tv_usec)/1000000.);
}


#include <cmath>

#ifndef PI
const double PI = 3.14159265358979323846;
#endif
const double TWOPI = 2.0*PI;

/**
 * Normalize angle to be within the interval [-pi,pi].
 */
inline double standardRad(double t) {
  if (t >= 0.) {
    t = fmod(t+PI, TWOPI) - PI;
  } else {
    t = fmod(t-PI, -TWOPI) + PI;
  }
  return t;
}

/**
 * Convert rotation matrix to Euler angles
 */
void wRo_to_euler(const Eigen::Matrix3d& wRo, double& yaw, double& pitch, double& roll) {
    yaw = standardRad(atan2(wRo(1,0), wRo(0,0)));
    double c = cos(yaw);
    double s = sin(yaw);
    pitch = standardRad(atan2(-wRo(2,0), wRo(0,0)*c + wRo(1,0)*s));
    roll  = standardRad(atan2(wRo(0,2)*s - wRo(1,2)*c, -wRo(0,1)*s + wRo(1,1)*c));
}


class Demo {  // TODO set constants up

  AprilTags::TagDetector* m_tagDetector;
  AprilTags::TagCodes m_tagCodes;

  bool m_draw; // draw image and April tag detections?
  bool m_arduino; // send tag detections to serial port?
  bool m_timing; // print timing information for each tag extraction call

  int m_width; // image size in pixels
  int m_height;
  double m_tagSize; // April tag side length in meters of square black frame 
  double m_fx; // camera focal length in pixels
  double m_fy;
  double m_px; // camera principal point
  double m_py;

  int m_deviceId; // camera id (in case of multiple cameras)

  list<string> m_imgNames;

  cv::VideoCapture m_cap;

  int m_exposure;
  int m_gain;
  int m_brightness;

  Serial m_serial;

public:

  // default constructor
  Demo() :
    // default settiwgs, most can be modified through command line options (see below)
    m_tagDetector(NULL),
    m_tagCodes(AprilTags::tagCodes36h11),

    m_draw(true),
    m_arduino(false),
    m_timing(false),

    m_width(640),
    m_height(480),
    //m_tagSize(0.00615), // in meters
    m_tagSize(0.00944),
    m_fx(667), // in pixels
    m_fy(666), //
    m_px(344), // principal point
    m_py(227),

    //m_tagSize(0.166), // in meters
    //m_fx(600), // in pixels
    //m_fy(600), //
    //m_px(m_width/2), // principal point
    //m_py(m_height/2),

    m_exposure(-1),
    m_gain(-1),
    m_brightness(-1),

    m_deviceId(0)
  {}

  // changing the tag family
  void setTagCodes(string s) {
    if (s=="16h5") {
      m_tagCodes = AprilTags::tagCodes16h5;
    } else if (s=="25h7") {
      m_tagCodes = AprilTags::tagCodes25h7;
    } else if (s=="25h9") {
      m_tagCodes = AprilTags::tagCodes25h9;
    } else if (s=="36h9") {
      m_tagCodes = AprilTags::tagCodes36h9;
    } else if (s=="36h11") {
      m_tagCodes = AprilTags::tagCodes36h11;
    } else {
      cout << "Invalid tag family specified" << endl;
      exit(1);
    }
  }

  // parse command line options to change default behavior
  void parseOptions(int argc, char* argv[]) {
    int c;
    while ((c = getopt(argc, argv, ":h?adtC:F:H:S:W:E:G:B:D:")) != -1) {
      // Each option character has to be in the string in getopt();
      // the first colon changes the error character from '?' to ':';
      // a colon after an option means that there is an extra
      // parameter to this option; 'W' is a reserved character
      switch (c) {
      case 'h':
      case '?':
        cout << intro;
        cout << usage;
        exit(0);
        break;
      case 'a':
        m_arduino = true;
        break;
      case 'd':
        m_draw = false;
        break;
      case 't':
        m_timing = true;
        break;
      case 'C':
        setTagCodes(optarg);
        break;
      case 'F':
        m_fx = atof(optarg);
        m_fy = m_fx;
        break;
      case 'H':
        m_height = atoi(optarg);
        m_py = m_height/2;
         break;
      case 'S':
        m_tagSize = atof(optarg);
        break;
      case 'W':
        m_width = atoi(optarg);
        m_px = m_width/2;
        break;
      case 'E':
#ifndef EXPOSURE_CONTROL
        cout << "Error: Exposure option (-E) not available" << endl;
        exit(1);
#endif
        m_exposure = atoi(optarg);
        break;
      case 'G':
#ifndef EXPOSURE_CONTROL
        cout << "Error: Gain option (-G) not available" << endl;
        exit(1);
#endif
        m_gain = atoi(optarg);
        break;
      case 'B':
#ifndef EXPOSURE_CONTROL
        cout << "Error: Brightness option (-B) not available" << endl;
        exit(1);
#endif
        m_brightness = atoi(optarg);
        break;
      case 'D':
        m_deviceId = atoi(optarg);
        break;
      case ':': // unknown option, from getopt
        cout << intro;
        cout << usage;
        exit(1);
        break;
      }
    }

    if (argc > optind) {
      for (int i=0; i<argc-optind; i++) {
        m_imgNames.push_back(argv[optind+i]);
      }
    }
  }

  void setup() {
    m_tagDetector = new AprilTags::TagDetector(m_tagCodes);

    // prepare window for drawing the camera images
    if (m_draw) {
      cv::namedWindow(windowName, 1);
    }

    // optional: prepare serial port for communication with Arduino
    if (m_arduino) {
      m_serial.open("/dev/ttyACM0");
    }
  }

  void setupVideo() {

#ifdef EXPOSURE_CONTROL
    // manually setting camera exposure settings; OpenCV/v4l1 doesn't
    // support exposure control; so here we manually use v4l2 before
    // opening the device via OpenCV; confirmed to work with Logitech
    // C270; try exposure=20, gain=100, brightness=150

    string video_str = "/dev/video1h i ";
    video_str[10] = '0' + m_deviceId;
    int device = v4l2_open(video_str.c_str(), O_RDWR | O_NONBLOCK);

    if (m_exposure >= 0) {
      // not sure why, but v4l2_set_control() does not work for
      // V4L2_CID_EXPOSURE_AUTO...
      struct v4l2_control c;
      c.id = V4L2_CID_EXPOSURE_AUTO;
      c.value = 1; // 1=manual, 3=auto; V4L2_EXPOSURE_AUTO fails...
      if (v4l2_ioctl(device, VIDIOC_S_CTRL, &c) != 0) {
        cout << "Failed to set... " << strerror(errno) << endl;
      }
      cout << "exposure: " << m_exposure << endl;
      v4l2_set_control(device, V4L2_CID_EXPOSURE_ABSOLUTE, m_exposure*6);
    }
    if (m_gain >= 0) {
      cout << "gain: " << m_gain << endl;
      v4l2_set_control(device, V4L2_CID_GAIN, m_gain*256);
    }
    if (m_brightness >= 0) {
      cout << "brightness: " << m_brightness << endl;
      v4l2_set_control(device, V4L2_CID_BRIGHTNESS, m_brightness*256);
    }
    v4l2_close(device);
#endif 

    // find and open a USB camera (built in laptop camera, web cam etc)
    m_cap = cv::VideoCapture(m_deviceId);
        if(!m_cap.isOpened()) {
      cerr << "ERROR: Can't find video device " << m_deviceId << "\n";
      exit(1);
    }
    m_cap.set(CV_CAP_PROP_FRAME_WIDTH, m_width);
    m_cap.set(CV_CAP_PROP_FRAME_HEIGHT, m_height);
    cout << "Camera successfully opened (ignore error messages above...)" << endl;
    cout << "Actual resolution: "
         << m_cap.get(CV_CAP_PROP_FRAME_WIDTH) << "x"
         << m_cap.get(CV_CAP_PROP_FRAME_HEIGHT) << endl;

  }

void print_detection(AprilTags::TagDetection& detection) const { //todo FAILING 
    const int TIMESTEP = 10;

    cout << "Id:" << detection.id;
         //<< "(Ham: " << detection.hammingDistance << ")";

    // recovering the relative pose of a tag:

    // NOTE: for this to be accurate, it is necessary to use the
    // actual camera parameters here as well as the actual tag size
    // (m_fx, m_fy, m_px, m_py, m_tagSize)

    Eigen::Vector3d translation;
    Eigen::Matrix3d rotation;
    detection.getRelativeTranslationRotation(m_tagSize, m_fx, m_fy, m_px, m_py,
                                             translation, rotation);

    Eigen::Matrix3d F;
    F <<
      1, 0,  0,
      0,  -1,  0,
      0,  0,  1;
    Eigen::Matrix3d fixed_rot = F*rotation;
    double yaw, pitch, roll;
    wRo_to_euler(fixed_rot, yaw, pitch, roll);

    time_t now = time(0);
    char* dt = ctime(&now); // get human readable current time
    *std::remove(dt, dt+strlen(dt), '\n') = '\0'; //remove newline
    cout << " [" << dt << "] distance,x,y,z,yaw,pitch,roll; "  //nrw
         << fixed << setprecision(6) << translation.norm()
         << "; " << translation(0)
         << "; " << translation(1)
         << "; " << translation(2)
         << "; " << yaw
         << "; " << pitch
         << "; " << roll << ";\n"
         << endl; //nrw

    // Write time and data to file upon any keypress
    if (cv::waitKey(1) >= 0) {
        cout << "wrote to file \n"; //nrw
        ofstream myfile;
        myfile.open ("data.txt", std::ios_base::app | std::ios_base::out); //open in append mode
        myfile <<  " [" << dt << "] distance,x,y,z,yaw,pitch,roll; "  //nrw
            << fixed << setprecision(6) << translation.norm()
            << "; " << translation(0)
            << "; " << translation(1)
            << "; " << translation(2)
            << "; " << yaw
            << "; " << pitch
            << "; " << roll << ";\n"
            << endl; //nrw
        flag = 1; //send signal to write image as well
      }


    // Also note that for SLAM/multi-view application it is better to
    // use reprojection error of corner points, because the noise in
    // this relative pose is very non-Gaussian; see iSAM source code
    // for suitable factors.
  }

  void processImage(cv::Mat& image, cv::Mat& image_gray) {
    // alternative way is to grab, then retrieve; allows for
    // multiple grab when processing below frame rate - v4l keeps a
    // number of frames buffered, which can lead to significant lag
    //      m_cap.grab();
    //      m_cap.retrieve(image);

    // detect April tags (requires a gray scale image)
    cv::cvtColor(image, image_gray, CV_BGR2GRAY);
    double t0;
    if (m_timing) {
      t0 = tic();
    }
    vector<AprilTags::TagDetection> detections = m_tagDetector->extractTags(image_gray);
    if (m_timing) {
      double dt = tic()-t0;
      cout << "Extracting tags took " << dt << " seconds." << endl;
    }

    // print out each detection
    //cout << detections.size() << " tags detected:" << endl;
    for (int i=0; i<detections.size(); i++) {
      print_detection(detections[i]);
    }

    // show the current image including any detections
    if (m_draw) {
        for (int i=0; i<detections.size(); i++) {
            // also highlight in the image
            detections[i].draw(image);
        }
        imshow(windowName, image); // OpenCV call
    }

    // optionally send tag information to serial port (e.g. to Arduino)
    if (m_arduino) {
      if (detections.size() > 0) {
        // only the first detected tag is sent out for now
        Eigen::Vector3d translation;
        Eigen::Matrix3d rotation;
        detections[0].getRelativeTranslationRotation(m_tagSize, m_fx, m_fy, m_px, m_py,
                                                     translation, rotation);
        m_serial.print(detections[0].id);
        m_serial.print(",");
        m_serial.print(translation(0));
        m_serial.print(",");
        m_serial.print(translation(1));
        m_serial.print(",");
        m_serial.print(translation(2));
        m_serial.print("\n");
      } else {
        // no tag detected: tag ID = -1
        m_serial.print("-1,0.0,0.0,0.0\n");
      }
    }
  }

  // Load and process a single image
  void loadImages() {
    cv::Mat image;
    cv::Mat image_gray;

    for (list<string>::iterator it=m_imgNames.begin(); it!=m_imgNames.end(); it++) {
      image = cv::imread(*it); // load image with opencv
      processImage(image, image_gray);
      while (cv::waitKey(100) == -1) {}
    }
  }

  // Video or image processing?
  bool isVideo() {
    return m_imgNames.empty();
  }

  // The processing loop where images are retrieved, tags detected,
  // and information about detections generated
  void loop() {

    cv::Mat image;
    cv::Mat image_gray;

    int frame = 0;
    double last_t = tic();
    while (true) {

      // capture frame
      m_cap >> image;

      processImage(image, image_gray);

      // print out the frame rate at which image frames are being processed
      frame++;
      if (frame % 10 == 0) {
          double t = tic();
          //cout << "  " << 10./(t-last_t) << " fps" << endl;
          last_t = t;
      }

      // exit if any key is pressed -- DISABLED by above loop capturing any key
      //if (cv::waitKey(1) >= 0) break;
      //if (cv::waitKey(1) == 'q') break;
      
      // Write image to file
      if (flag == 1){
          cout << "wrote to file \n"; //nrw
          time_t now = time(0);
          char* dt = ctime(&now); // get human readable current time
          *std::remove(dt, dt+strlen(dt), '\n') = '\0'; //remove newline
          std::string str(dt);
          str += "_image.jpg";
          imwrite( str, image );
          flag = 0;
      }
    }
  }

}; // Demo


// here is were everything begins
int main(int argc, char* argv[]) {
    ofstream myfile;
    myfile.open("data.txt", std::ios_base::app | std::ios_base::out); //open in append mode
    time_t now = time(0);
    char* dt = ctime(&now); // get human readable current time
    *std::remove(dt, dt+strlen(dt), '\n') = '\0'; //remove newline
    myfile << " [" << dt << "] BEGIN DATA COLLECTION ";  //This does not work anymore, not sure why //nrw

    Demo demo;

    // process command line options
    demo.parseOptions(argc, argv);

    demo.setup();
    if (demo.isVideo()) {
        cout << "Processing video" << endl;

        // setup image source, window for drawing, serial port...
        demo.setupVideo();

        // the actual processing loop where tags are detected and visualized
        demo.loop();

    } else {
        cout << "Processing image" << endl;

        // process single image
        demo.loadImages();

  }

  return 0;
}
