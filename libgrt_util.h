#ifndef _CSVIO_H_
#define _CSVIO_H_

#include <GRT.h>
#include <iostream>
#include <climits>
#include <locale> // for isspace
#include <string>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
using namespace GRT;

#define csvio_dispatch(io,func,args...) switch(io.type) {\
  case TIMESERIES:\
    func( io.t_data, ##args );\
    break;\
  case CLASSIFICATION:\
    func( io.c_data, ##args );\
    break;\
  default:\
    throw invalid_argument("unknown data type in dispatch");\
    break;\
}

//typedef enum { TIMESERIES, UNLABELLED, REGRESSION, CLASSIFICATION, UNKNOWN } csv_type_t;
typedef enum { TIMESERIES, CLASSIFICATION, UNKNOWN } csv_type_t;

class CsvIOSample {
  public:
    csv_type_t type;
    UnlabelledData u_data;
    RegressionData r_data;
    ClassificationSample c_data;
    TimeSeriesClassificationSample t_data;
    std::vector<string> labelset;
    bool has_NULL_label;
    int linenum;

    static bool iscomment(std::string line) {
      for (int i=0; i<line.length(); i++) {
        char c = line[i];
        if (std::isspace(c))
          continue;
        return c=='#';
      }
    }

    static bool isempty(std::string line) {
      for (int i=0; i<line.length(); i++) {
        char c=line[i];
        if (!std::isspace(c))
          return false;
      }
      return true;
    }

    int classkey(std::string label) {
      int i = std::find(labelset.begin(), labelset.end(), label) == labelset.end() ? -1 :
              std::find(labelset.begin(), labelset.end(), label) - labelset.begin();
      if (i==-1) {
        labelset.push_back(label);
        return labelset.size()-1;
      }
      if (!has_NULL_label && i==0)
        has_NULL_label = true;
      return i;
    }

    friend std::istream& operator>> (std::istream &in, CsvIOSample &o) 
    {
      using namespace std;

      string line, label;
      vector<vector<double>> data;
      double d;

      for (o.linenum=0; getline(in,line); o.linenum++) {
        stringstream ss(line);

        ss >> label;

        if (line == "") {
          if (data.size()!=0)
            break;
          else
            continue;
        }

        if (label[0] == '#') {
          if (o.type==UNKNOWN) o.settype(line);
          in.ignore(INT_MAX, '\n');
          continue;
        }

        if (o.type==UNKNOWN)
          throw new invalid_argument("dunno how to parse data");

        vector<double> sample;
        while (ss >> d)
          sample.push_back(d);
        data.push_back(sample);

        if (o.type!=TIMESERIES)
          break;
      }

      if (data.size() > 0) {
        switch(o.type) {
        case TIMESERIES: {
          MatrixDouble md(data.size(), data.back().size());
          md = data;
          o.t_data = TimeSeriesClassificationSample(o.classkey(label), md);
          break; }
        case CLASSIFICATION:
          o.c_data = ClassificationSample(o.classkey(label), data.back());
          break;
        default:
          throw invalid_argument("unknown data type");
        }
      }

      if (data.size() != 0) in.clear();
      return in;
    }

    CsvIOSample(const std::string &t) {
      settype(t);
      linenum = 0;
      has_NULL_label = false;
      labelset.push_back("NULL");
    }

  protected:
  void settype(const std::string &t) {
    using namespace std;
    if (t.find("classification") != string::npos)
      type = CLASSIFICATION;
      //else if ("unlabelled" == type)
      //  o.type = UNLABELLED;
      //else if ("regression" == type)
      //  type = REGRESSION;
    else if (t.find("timeseries") != string::npos)
      type =  TIMESERIES;
    else
      type = UNKNOWN;
  }
};

class CollectDataset
{
  public:
  TimeSeriesClassificationData t_data;
  ClassificationData c_data;
  csv_type_t type;

  CollectDataset() {
    t_data.setAllowNullGestureClass(true);
    c_data.setAllowNullGestureClass(true);
    type = UNKNOWN;
  }

  bool add(TimeSeriesClassificationSample &sample, vector<string> &labels) {
    type = TIMESERIES;
    UINT cl = sample.getClassLabel();

    if (t_data.getNumDimensions() == 0)
      t_data.setNumDimensions(sample.getData().getNumCols());

    if (!t_data.addSample(sample.getClassLabel(), sample.getData()))
      return false;
    t_data.setClassNameForCorrespondingClassLabel(labels[cl], cl);
    return true;
  }

  bool add(ClassificationSample &sample, vector<string> &labels) {
    type = CLASSIFICATION;
    UINT cl = sample.getClassLabel();

    if (c_data.getNumDimensions() == 0)
      c_data.setNumDimensions(sample.getSample().size());

    if (!c_data.addSample(sample.getClassLabel(), sample.getSample()))
      return false;
    c_data.setClassNameForCorrespondingClassLabel(labels[cl], cl);
    return true;
  }

  std::string getStatsAsString() {
    switch(type) {
    case TIMESERIES:
      return t_data.getStatsAsString();
    case CLASSIFICATION:
      return c_data.getStatsAsString();
    default:
      return "unknown datatype";
    }
  }
};

static bool is_running = true;
  /* enable logging output */
void set_verbosity(int level) {
  DebugLog::enableLogging(false);
  TrainingLog::enableLogging(false);
  TestingLog::enableLogging(false);
  InfoLog::enableLogging(false);
  WarningLog::enableLogging(false);
  ErrorLog::enableLogging(false);
  switch(level) {
      case 4: DebugLog::enableLogging(true);
      case 3: TestingLog::enableLogging(true); TrainingLog::enableLogging(true);
      case 2: InfoLog::enableLogging(true);
      case 1: WarningLog::enableLogging(true);
      case 0: ErrorLog::enableLogging(true);
  }
}


static bool *is_running_indicator;

void sig_callback(int signum) {
  if (is_running_indicator)
    *is_running_indicator = false;
}

static void set_running_indicator(bool *is_running_ind)
{
    struct sigaction action;
    is_running_indicator = is_running_ind;
    action.sa_handler = sig_callback;
    action.sa_flags = 0;
    sigemptyset (&action.sa_mask);
    sigaction (SIGINT, &action, NULL);
    sigaction (SIGTERM, &action, NULL);
}

/* we just try to load with every avail classifier */
Classifier *loadFromFile(string &file)
{
  ErrorLog err;
  Classifier *classifier = NULL;
  bool errlog = err.getLoggingEnabled();
  ErrorLog::enableLogging(false);

  for (string c : Classifier::getRegisteredClassifiers()) {
    classifier = Classifier::createInstanceFromString(c);
    if (classifier->loadModelFromFile(file))
      break;
    classifier = NULL;
  }

  ErrorLog::enableLogging(true);
  return classifier;
}


#endif
