/**
* This file is part of ORB-SLAM2.
*
* Copyright (C) 2014-2016 Raúl Mur-Artal <raulmur at unizar dot es> (University of Zaragoza)
* For more information see <https://github.com/raulmur/ORB_SLAM2>
*
* ORB-SLAM2 is free software: you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation, either version 3 of the License, or
* (at your option) any later version.
*
* ORB-SLAM2 is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
* GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License
* along with ORB-SLAM2. If not, see <http://www.gnu.org/licenses/>.
*/


#include<iostream>
#include<algorithm>
#include<fstream>
#include<chrono>

#include<opencv2/core/core.hpp>

#include<System.h>
#include "IMU/imudata.h"
#include <unistd.h>
#include "sys/time.h"

using namespace std;
using namespace ORB_SLAM2;

void LoadImages(const string &strImagePath, const string &strPathTimes,
                vector<string> &vstrImages, vector<double> &vTimeStamps);

void LoadImus(const string &strImuPath, vector<IMUData> &vImus);

int main(int argc, char **argv)
{
    if(argc != 6 && argc != 7)
    {
        cerr << endl << "Usage: ./mono_tum path_to_vocabulary path_to_settings path_to_image_folder path_to_times_file" << endl;
        return 1;
    }


    vector<IMUData> vImus;
    LoadImus(argv[5], vImus);
    int nImus = vImus.size();
    cout << "Imus in data: " << nImus << endl;
    if (nImus <= 0) {
        cerr << "ERROR: Failed to load imus" << endl;
        return 1;
    }

    // Retrieve paths to images
    vector<string> vstrImageFilenames;
    vector<double> vTimestamps;
    LoadImages(string(argv[3]), string(argv[4]), vstrImageFilenames, vTimestamps);

    int nImages = vstrImageFilenames.size();
    if(nImages<=0)
    {
        cerr << "ERROR: Failed to load images" << endl;
        return 1;
    }

    if (argc == 7)
        nImages = atoi(argv[6]);
    
    cout << "process images number: " << nImages << endl;

    // Create SLAM system. It initializes all system threads and gets ready to process frames.
    ORB_SLAM2::System SLAM(argv[1],argv[2],ORB_SLAM2::System::MONOCULAR,true);

    // Vector for tracking time statistics
    vector<float> vTimesTrack;
    vTimesTrack.resize(nImages);

    cout << endl << "-------" << endl;
    cout << "Start processing sequence ..." << endl;
    cout << "Images in the sequence: " << nImages << endl << endl;

    int imagestart = 0;
    const double startimutime = vImus[0]._t;
    cout << fixed << "start imu time: " << startimutime << endl;
    cout << fixed << "start image ime: " << vTimestamps[0] << endl;

    while (1) {
        if (startimutime <= vTimestamps[imagestart])
            break;
        imagestart++;
    }
    cout << "image start: " << imagestart << endl;
    long imuindex = 0;

    // Main loop
    cv::Mat im;
    uint32_t mBenchTime = 0;
    uint32_t mCurrFrame = 0;

    for(int ni=imagestart; ni<nImages; ni++)
    {
        // Read image from file
        im = cv::imread(vstrImageFilenames[ni],CV_LOAD_IMAGE_UNCHANGED);
        double tframe = vTimestamps[ni];

        vector<IMUData> vimu;
        while (1) {
            const IMUData& imudata = vImus[imuindex];
            if (imudata._t >= tframe)
                break;

            vimu.push_back(imudata);
            imuindex++;
        }

        if(im.empty())
        {
            cerr << endl << "Failed to load image at: "
                 <<  vstrImageFilenames[ni] << endl;
            return 1;
        }

#ifdef COMPILEDWITHC11
        std::chrono::steady_clock::time_point t1 = std::chrono::steady_clock::now();
#else
        std::chrono::monotonic_clock::time_point t1 = std::chrono::monotonic_clock::now();
#endif

        // Pass the image to the SLAM system
        //SLAM.TrackMonocular(im,tframe);
        SLAM.TrackMonoVI(im, vimu, tframe);

#ifdef COMPILEDWITHC11
        std::chrono::steady_clock::time_point t2 = std::chrono::steady_clock::now();
#else
        std::chrono::monotonic_clock::time_point t2 = std::chrono::monotonic_clock::now();
#endif
        struct timeval tv;
        uint32_t time, benchmark_interval = 5;

        gettimeofday(&tv, NULL);
        time = tv.tv_sec * 1000 + tv.tv_usec / 1000;
        mCurrFrame++;
        if (mCurrFrame == 0)
            mBenchTime = time;
        if (time - mBenchTime > (benchmark_interval * 1000)) {
            printf("%d frames in %d seconds: %f fps\n",
                    mCurrFrame,
                    benchmark_interval,
                    (float) mCurrFrame / benchmark_interval);
            mBenchTime = time;
            mCurrFrame = 0;
        }

        double ttrack= std::chrono::duration_cast<std::chrono::duration<double> >(t2 - t1).count();

        vTimesTrack[ni]=ttrack;

        // Wait to load the next frame
        double T=0;
        if(ni<nImages-1)
            T = vTimestamps[ni+1]-tframe;
        else if(ni>0)
            T = tframe-vTimestamps[ni-1];

        if(ttrack<T)
            usleep((T-ttrack)*1e6);
    }

    // Stop all threads
    SLAM.Shutdown();

    // Tracking time statistics
    sort(vTimesTrack.begin(),vTimesTrack.end());
    float totaltime = 0;
    for(int ni=0; ni<nImages; ni++)
    {
        totaltime+=vTimesTrack[ni];
    }
    cout << "-------" << endl << endl;
    cout << "median tracking time: " << vTimesTrack[nImages/2] << endl;
    cout << "mean tracking time: " << totaltime/nImages << endl;

    // Save camera trajectory
    SLAM.SaveKeyFrameTrajectoryTUM("KeyFrameTrajectory.txt");

    return 0;
}

void LoadImus(const string &strImuPath, vector<IMUData> &vImus)
{
    ifstream fImus;
    fImus.open(strImuPath.c_str());
    vImus.reserve(30000);

    while (!fImus.eof()) {
        string s;
        getline(fImus, s);
        if (!s.empty()) {
            char c = s.at(0);
            if (c < '0' || c >'9')
                continue;

            stringstream ss;
            ss << s;
            double tmpd;
            int cnt = 0;
            double data[10];
            while (ss >> tmpd) {
                data[cnt] = tmpd;
                cnt++;
                if (cnt == 7)
                    break;
                if (ss.peek() == ',' || ss.peek() == ' ')
                    ss.ignore();
            }

            data[0] *= 1e-9;
            IMUData imudata(data[1], data[2], data[3], data[4], data[5], data[6], data[0]);
            vImus.push_back(imudata);
        }
    }
}


void LoadImages(const string &strImagePath, const string &strPathTimes,
                vector<string> &vstrImages, vector<double> &vTimeStamps)
{
    ifstream fTimes;
    fTimes.open(strPathTimes.c_str());
    vTimeStamps.reserve(5000);
    vstrImages.reserve(5000);
    while(!fTimes.eof())
    {
        string s;
        getline(fTimes,s);
        if(!s.empty())
        {
            stringstream ss;
            ss << s;
            vstrImages.push_back(strImagePath + "/" + ss.str() + ".png");
            double t;
            ss >> t;
            vTimeStamps.push_back(t/1e9);

        }
    }
}
