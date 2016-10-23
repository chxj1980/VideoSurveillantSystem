﻿#include "leavedetection.h"
#include <iostream>
#include <QDateTime>
#include <QDir>
#include <QDebug>

LeaveDetection::LeaveDetection(QObject *parent) : QObject(parent)
{
    initData();//初始化数据
    std::cout<<"LeaveDetection()"<<std::endl;
}

LeaveDetection::~LeaveDetection()
{
    if(imageProcess)
    {
        delete imageProcess;
    }
    if(geometryCalculations)
    {
        delete geometryCalculations;
    }
    if(frameForeground)
    {
        delete frameForeground;
    }
    if(pictureSaveThread)
    {
        pictureSaveThread->wait();
        delete pictureSaveThread;
        pictureSaveThread=NULL;
    }
    std::cout<<"~LeaveDetection()"<<std::endl;
}

//离岗检测
int LeaveDetection::detect(cv::Mat &frame)
{
    cv::Mat roiArea;//办公区域截图
    int objectNumber=0;
    std::vector<cv::Rect> objectRect;//检测的目标矩形边界
    currentDate = QDateTime::currentDateTime().toString("yyyy:MM:dd-hh:mm:ss");
    int isLeave=0;
    errorCode=0;
    if(isFirstRun)
    {
        saveConfig();
        isFirstRun=false;
    }
    if(!frame.empty())
    {
        for(int loop=0;loop<(int)office.size();loop++)
        {
            objectNumber=0;
            if(maxLeaveCount<0||office[loop].getLeaveCount()<maxLeaveCount)
            {
              roiArea=frame(office[loop].getPolygonRect());
              objectRect.clear();
              objectRect=detectObject(roiArea);
              matchOfficeArea(objectRect,loop);
              isLeave=leaveArea(loop);
              objectNumber+=(int)objectRect.size();
              if(errorCode<0)
              {
                  return errorCode;
              }
              else
              {
                  return isLeave;
              }
            }
        }
    }
    return 0;
}

//初始化检测参数
void LeaveDetection::initDetectData()
{
    isFirstRun=true;
    errorCode=0;
    office.clear();

    loadConfig();
    frameForeground->initData();
    OfficeArea area;
    int number=(int)pointsArea.size();
    for(int loop1=0;loop1<number;loop1++)
    {
        area.setPolygon(pointsArea[loop1]);
        area.setLeaveCount(0);
        area.setLeaveTime(0);
        area.isFirstLeave=0;
        office.push_back(area);
    }
}

//通过检测出的目标匹配目标区域
void LeaveDetection::matchOfficeAllArea(std::vector<cv::Rect> vectorRect)
{
    size_t numberOfficeArea=office.size();
    //判断两矩形的相交度
    for(int loop=0;loop<numberOfficeArea;loop++)
    {
        matchOfficeArea(vectorRect,loop);
    }
}

//对某一个区域匹配目标
void LeaveDetection::matchOfficeArea(std::vector<cv::Rect> vectorRect,int number)
{
    int deltaTime=0;
    int numberRect=(int)vectorRect.size();
    float maxCrossValue=0.0;
    float intersectionValue=0.0f;//两个矩形的相交度
    std::vector<cv::Point> rectPolygon;
    rectPolygon.clear();
    for(int loop=0;loop<numberRect;loop++)
    {
        rectPolygon.clear();
        rectPolygon=geometryCalculations->covRectPolygon(vectorRect[loop]);//矩形转多边形表示
        //得到两多边形的相交度
        intersectionValue=geometryCalculations->polygonOverlap(office[number].getPolygon1(),rectPolygon);
        //qDebug()<<"crossvalue:"<<intersectionValue;
        if(intersectionValue>maxCrossValue)
        {
            maxCrossValue=intersectionValue;
        }
    }
    qDebug()<<"maxCrossValue:"<<maxCrossValue;
    if(maxCrossValue>crossMatchMaxValue)
    {
        office[number].setLeaveTime(0);
        office[number].isFirstLeave=0;
    }
    else
    {
        if(office[number].isFirstLeave==0)
        {
            office[number].firstLeaveTime=QDateTime::currentDateTime();
            office[number].isFirstLeave=1;
        }
        else
        {
            deltaTime=office[number].firstLeaveTime.secsTo(QDateTime::currentDateTime());
            qDebug()<<"deltaTime:"<<deltaTime;
            office[number].setLeaveTime(deltaTime);
        }
    }
}

//判断某个区域是否离岗
int LeaveDetection::leaveArea(int number)
{
    if(office[number].getLeaveTime()>maxLeaveTime)
    {
        office[number].setLeaveTime(0);
        office[number].isFirstLeave=0;
        office[number].addLeaveCount(1);
        return number+1;
    }
    else
    {
        return 0;
    }
}

//得到离岗区域并保存图片
QList<int> LeaveDetection::allLeaveArea(cv::Mat inFrame,QString leaveFileDir,QString leaveFileName)
{
    int isLeave=0;
    QList<int> leaveAreaAll;
    int numberOfficeArea=(int)office.size();
    leaveAreaAll.clear();
    for(int loop=0;loop<numberOfficeArea;loop++)
    {
       isLeave=leaveArea(inFrame,loop,leaveFileDir,leaveFileName);
       if(isLeave>0)
       {
           leaveAreaAll.append(isLeave);
       }
    }
    return leaveAreaAll;
}

//该区域是否离岗并保存图片
int LeaveDetection::leaveArea(cv::Mat inFrame,int number,QString leaveFileDir,QString leaveFileName)
{
    //Mat leaveROI;
    if(office[number].getLeaveTime()>maxLeaveTime)
    {
        //leaveROI=inFrame(office[number].getPolygonRect());
        errorCode=pictureSaveThread->initData(leaveFileDir,leaveFileName,inFrame);
        pictureSaveThread->start();
        office[number].setLeaveTime(0);
        office[number].isFirstLeave=0;
        office[number].addLeaveCount(1);
        return number+1;
    }
    else
    {
        return 0;
    }
}

//绘制办公区域
void LeaveDetection::drawOfficeArea(cv::Mat &inFrame ,cv::Scalar color)
{
    int number=(int)office.size();
    int index=0;
    std::vector<cv::Point> polygon;
    polygon.clear();
    for(int loop1=0;loop1<number;loop1++)
    {
        polygon=office[loop1].getPolygon();
        int num=(int)polygon.size();
        for(int loop2=0;loop2<num;loop2++)
        {
            index=(loop2+1)%num;
            cv::line(inFrame,polygon[loop2],polygon[index],color,2,8);
        }
    }
}

//检测运动目标
std::vector<cv::Rect> LeaveDetection::detectObject(cv::Mat &frame)
{
    std::vector<cv::Rect> objectRect;//检测的目标矩形边界
    objectRect.clear();
    if(frameForeground)
    {
        objectRect=frameForeground->getFrameForegroundRect(frame,minBox);
        if(isDrawObject)
        {
            imageProcess->drawRect(frame,objectRect,cv::Scalar(0,0,255));
        }
    }

    return objectRect;
}

//得到错误码
int LeaveDetection::getErrorCode()
{
    return errorCode;
}

//得到办公区域
std::vector<OfficeArea>  LeaveDetection::getOfficeArea()
{
    return office;
}

//初始化数据
void LeaveDetection::initData()
{
    errorCode=0;//错误码
    isFirstRun=true;//第一次运行

    pictureSaveThread=new PictureSaveThread();//图片保存类
    imageProcess=new ImageProcess();//图像处理算法类
    geometryCalculations=new GeometryCalculations();//几何运算类
    frameForeground=new FrameForeground();//前景检测类

    office.clear();
    pointsArea.clear();

    savePictureFileName="image.png";

    loadConfig();

}

void LeaveDetection::saveConfig()
{
    cv::FileStorage fs;
    QDir makeDir;
    if(!makeDir.exists("./config/"))
    {
        if(!makeDir.mkdir("./config/"))
        {
            std::cout<<"make dir fail!"<<std::endl;
            return;
        }
    }
    fs.open("./config/LeaveDetection.xml",cv::FileStorage::WRITE,"utf-8");

    cv::write(fs,"isDrawObject",isDrawObject);
    cv::write(fs,"maxLeaveCount",maxLeaveCount);
    cv::write(fs,"maxLeaveTime",maxLeaveTime);
    cv::write(fs, "crossMatchMaxValue", crossMatchMaxValue);
    cv::write(fs, "minBox", minBox);
    cv::write(fs,"savePictureDir",savePictureDir.toStdString());

    for(int loop=0;loop<(int)pointsArea.size();loop++)
    {
        QString tempName="pointsArea"+QString::number(loop);
        cv::write(fs,tempName.toStdString().c_str(),pointsArea[loop]);
    }
    fs.release();
}

void LeaveDetection::loadConfig()
{
    cv::FileStorage fs;
    std::vector<cv::Point> tempVector;
    cv::String tempDataDir="./leave";
    pointsArea.clear();
    fs.open("./config/LeaveDetection.xml",cv::FileStorage::READ,"utf-8");

    cv::read(fs["isDrawObject"],isDrawObject,true);
    cv::read(fs["maxLeaveCount"],maxLeaveCount,-1);
    cv::read(fs["maxLeaveTime"],maxLeaveTime,5*60);
    cv::read(fs["crossMatchMaxValue"], crossMatchMaxValue,0.1f);
    cv::read(fs["minBox"],minBox,300);
    cv::read(fs["savePictureDir"],tempDataDir,cv::String("./leave"));
    savePictureDir=QString::fromStdString(tempDataDir);
    qDebug()<<savePictureDir;

    cv::FileNode node=fs["pointsArea0"];
    if(node.isNone())
    {
        return;
    }
    cv::FileNodeIterator iterator=node.begin(),iterator_end=node.end();
    for(int loop=0;iterator!=iterator_end;iterator++,loop++)
    {
        QString tempName="pointsArea"+QString::number(loop);
        if(fs[tempName.toStdString().c_str()].isNone())
            break;
        cv::read(fs[tempName.toStdString().c_str()],tempVector);
        pointsArea.push_back(tempVector);
    }

    fs.release();
}

