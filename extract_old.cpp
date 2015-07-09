#include <GRT.h>
#include <iostream>
#include "cmdline.h"
#include "libgrt_util.h"

using namespace GRT;
using namespace std;

string list_extractors();
FeatureExtraction* apply_cmdline_args(string, cmdline::parser&, int, string&, int&);
bool feature(FeatureExtraction *f, std::string line, int buffer_size=0);

InfoLog info;

int main(int argc, const char *argv[]) {
  cmdline::parser c;
  int buffer_size=0;

  c.add<int>   ("verbose",    'v', "verbosity level: 0-4", false, 0);
  c.add        ("help",       'h', "print this message");
  c.add<int>   ("num-samples",'n', "number of input samples to use for training", false, 0);
  c.add<string>("output",     'o', "if given store the model in file, for later usage", false);
  c.footer     ("<feature-extractor or input file name> [<filename>] ");

  bool parse_ok = c.parse(argc, argv, false)  && !c.exist("help");
  set_verbosity(c.get<int>("verbose"));

  /* load the extractor and check wether we need to list them */
  string str_extractor = "list",
            input_file = "-";

  if (c.rest().size() > 0) {
    str_extractor = c.rest()[0];
    c.rest().erase(c.rest().begin());
  }

  if (str_extractor == "list") {
    cout << c.usage() << endl;
    cout << list_extractors();
    return 0;
  }

  /* try and create an instance by loading the str_extractor */
  FeatureExtraction *f = apply_cmdline_args(str_extractor,c,1,input_file, buffer_size);
  if (f == NULL) return -1; 

  if (!parse_ok) {
    cerr << c.usage() << endl << c.error() << endl;
    return -1;
  }

  /* do we read from a file or from stdin? */
  std::ifstream fin; fin.open(input_file);
  istream &in = input_file=="-" ? cin : fin;
  if (!in.good()) {
    cerr << "unable to read input file " << input_file << endl;
    return -1;
  }

  if (f!=NULL && f->getTrained() && c.exist("output")) {
    cerr << "refusing to load and store already trained model" << endl;
    return -1;
  }

  if (f!=NULL && f->getTrained() && c.exist("num-samples")) {
    cerr << "limiting the number of training samples on already trained extractor makes no sense" << endl;
    return -1;
  }

  string line;

  /* read the number of training samples */
  if (f==NULL || !f->getTrained()) {
    /* check if the number of input is limited */
    int num_training_samples = c.get<int>("num-samples"), num_lines = 0;
    vector<string> lines;
    vector<vector<double>> set;

    while (getline(in, line) && (num_training_samples==0 || num_lines < num_training_samples)) {
      stringstream ss(line);
      vector<double> data; double value;
      string label;

      lines.push_back(line);

      if (line=="" || line[0]=='#')
        continue;

      ss >> label;
      while (ss >> value)
        data.push_back(value);

      if (data.size() == 0)
        continue;

      if (num_lines == 0) {
        // initialization needs to be delayed until we know the number of input
        // dimensions, since they need to be known beforehand
        f = apply_cmdline_args(str_extractor, c, data.size(),input_file, buffer_size);
        if (f == NULL) {
          cerr << "unable to load extractor" << endl;
          return -1;
        }
      }

      num_lines++;
      set.push_back(data);
    }

    MatrixDouble dataset(set);

    if (!f->train(dataset))
      cerr << "WARNING: training the extractor failed, not of all them can be trained!" << endl;

    /* if there is a model file store it */
    if (c.exist("output"))
      f->save(c.get<string>("output"));

    /* and print what we already consumed */
    for (auto line : lines)
      if (!feature( f, line, buffer_size ))
        return -1;
  }

  /* now transforms the rest of the input */
  while (getline(in, line))
    if (!feature( f, line, buffer_size ))
      return -1;
}

string list_extractors() {
  stringstream ss;

  for( auto name : FeatureExtraction::getRegisteredFeatureExtractors())
    ss << name << endl;

  return ss.str();
}

FeatureExtraction*
apply_cmdline_args(string type, cmdline::parser &c, int num_dimensions, string &input_file, int &buffer_size) {
  FeatureExtraction *f;
  cmdline::parser p;

  if ( type == "RBMQuantizer" || type == "SOMQuantizer" || type == "KMeansQuantizer" )
    p.add<int> ("clusters", 'K', "the number of clusters", false, 10);

  else if ( type == "FFT" ) {
    p.add<int>    ("window",       'W', "window size in samples (should be power of two)", false, 512);
    p.add<int>    ("hop",          'H', "every Hth sample the FFT will be computed", false, 1);
    p.add<string> ("func",         'F', "window function, one of 'rect', 'bartlett','hamming','hanning'", false, "rect", cmdline::oneof<string>("rect","bartlett","hamming","hanning"));
    p.add         ("no-magnitude", 'M', "if magnitude should not be computed");
    p.add         ("no-phase",     'P', "set if phase should not be computed");
  }

  else if ( type == "FFTFeatures" ) {
    p.add<int>    ("window",          'W', "window size in samples (should be power of two)", false, 512);
    p.add         ("no-max-freq",     'N', "do not compute largest frequency");
    p.add         ("no-max-freq-spec",'M', "do not compute maximum-frequency spectrum frequency feature");
    p.add         ("no-centroid"   ,  'C', "do not compute center frequency");
    p.add<int>    ("top-K",           'K', "compute the top-K frequencies (0 disabels this feature)", false, 10);
  }

  else if ( type == "KMeansFeatures" ) 
    p.add<string> ("clusters",   'K', "list of cluster numbers as a comma-separated list", false, "1,100");

  else if ( type == "MovementIndex" )
    p.add<int>   ("samples",     'K', "number of samples hold to compute movement index", false, 100);

  else if ( type == "MovementTrajectoryFeatures" ) {
    p.add<int>   ("samples",     'K', "number of sample for trajectory computation", false, 100);
    p.add<int>   ("centroids",   'C', "number of centroids to calculate", false, 10);
    p.add<string>("feature-mode",'M', "feature mode: centroid,normalized,derivative,angle_2d", false, "centroid", cmdline::oneof<string>("centroid","normalized","derivative","angle_2d"));
    p.add<int>   ("histograms",  'H', "number of histogram bins", false, 10);
    p.add        ("start-end",   'S', "use trajectory start and end values");
    p.add        ("no-weighted-magnitude", 'W', "do not weight the computed magnitude");
  }

  else if ( type == "TimeDomainFeatures" ) {
    p.add<int>   ("samples",     'K', "number of sample for computation", false, 100);
    p.add<int>   ("frames",      'F', "frames for computation", false, 10);
    p.add        ("offset",      'O', "wether to offset the input");
    p.add        ("no-mean",     'M', "do not compute mean");
    p.add        ("no-stddev",   'S', "do not compute standard deviation");
    p.add        ("no-euclidean",'E', "do not compute euclidean norm");
    p.add        ("no-rms",      'R', "do not compute root mean square");
  }

  else if ( type == "TimeseriesBuffer" ) {
    p.add<int>   ("samples",     'K', "number of sample for computation", false, 100);
  }

  else if ( type == "ZeroCrossingCounter" ) {
    p.add<int>   ("window",      'W', "size of search window in samples", false, 20);
    p.add<double>("deadzone",    'D', "deadzone threshold", false, 0.01);
    p.add        ("combined",    'C', "wether zero-crossing are calculated independently");
  }
  else if ( type == "FrequencyDomainFeatures" ) {
    p.add<int>    ("hop",             'H', "every Hth sample the FFT will be computed", false, 1);
    p.add<string> ("func",            'F', "window function, one of 'rect', 'bartlett','hamming','hanning'", false, "rect", cmdline::oneof<string>("rect","bartlett","hamming","hanning"));
    p.add<int>    ("window",          'W', "window size in samples (should be power of two)", false, 512);
    p.add         ("no-max-freq",     'N', "do not compute largest frequency");
    p.add         ("no-max-freq-spec",'M', "do not compute maximum-frequency spectrum frequency feature");
    p.add         ("no-centroid"   ,  'C', "do not compute center frequency");
    p.add<int>    ("top-K",           'K', "compute the top-K frequencies (0 disabels this feature)", false, 10);
  }

  if (!p.parse(c.rest_with_name())) {
    cerr << c.usage() << endl << "feature extraction options:" << endl << p.str_options() << endl << p.error() << endl;
    return NULL;
  }

  if (c.exist("help")) {
    cerr << c.usage() << endl << "feature extraction options:" << endl << p.str_options();
    return NULL;
  }

  try { buffer_size = p.get<int>("samples"); } catch(...) {}
  try { buffer_size = p.get<int>("window"); } catch(...) {}

  if ( type == "RBMQuantizer" )
    f = new RBMQuantizer( p.get<int>("clusters") );

  else if ( type == "SOMQuantizer" )
    f = new SOMQuantizer( p.get<int>("clusters") );

  else if ( type == "KMeansQuantizer" )
    f = new KMeansQuantizer( p.get<int>("clusters") );

  else if ( type == "FFT" ) {
    vector<string> list = {"rect","bartlett","hamming","hanning"};
    f = new FFT(
        p.get<int>("window"),
        p.get<int>("hop"),
        num_dimensions,
        find(list.begin(),list.end(),p.get<string>("func")) - list.begin(),
        !p.exist("no-magnitude"),
        !p.exist("no-phase"));

  } else if ( type == "FFTFeatures" ) {
    f = new FFTFeatures(
        p.get<int>("window"),
        num_dimensions, // this must actuallcy be the numdiems from the FFT
        !p.exist("no-max-freq"),
        !p.exist("no-max-freq-spec"),
        !p.exist("no-centroid"),
        p.get<int>("top-K")!=0,
        p.get<int>("top-K"));

  } else if ( type == "FrequencyDomainFeatures" ) {
    vector<string> list = {"rect","bartlett","hamming","hanning"};
    f = new FrequencyDomainFeatures(
        p.get<int>("window"),
        p.get<int>("hop"),
        num_dimensions,
        find(list.begin(),list.end(),p.get<string>("func")) - list.begin(),
        !p.exist("no-max-freq"),
        !p.exist("no-max-freq-spec"),
        !p.exist("no-centroid"),
        p.get<int>("top-K")!=0,
        p.get<int>("top-K"));

  } else if ( type == "KMeansFeatures" ) {
    stringstream ss(p.get<string>("clusters"));
    vector<unsigned int> list;
    string token;
    while(getline(ss,token,','))
      list.push_back(stoi(token));
    f = new KMeansFeatures(list);

  } else if ( type == "MovementIndex" ) {
    f = new MovementIndex(p.get<int>("samples"),
        num_dimensions);

  } else if ( type == "MovementTrajectoryFeatures" ) {
    vector<string> list = {"centroid","normalized","derivative","angle_2d"};
    f = new MovementTrajectoryFeatures(
        p.get<int>("samples"),
        p.get<int>("centroids"),
        find(list.begin(), list.end(), p.get<string>("feature-mode")) - list.begin(),
        p.get<int>("histograms"),
        num_dimensions,
        p.exist("start-end"),
        p.exist("no-weighted-magnitude"));

  } else if ( type == "TimeDomainFeatures" ) {
    f = new TimeDomainFeatures(
        p.get<int>("samples"),
        p.get<int>("frames"),
        num_dimensions,
        p.exist("offset"),
        !p.exist("no-mean"),
        !p.exist("no-stddev"),
        !p.exist("no-euclidean"),
        !p.exist("no-rms"));

  } else if ( type == "TimeseriesBuffer" ) {
    f = new TimeseriesBuffer(
        p.get<int>("samples"),
        num_dimensions);

  } else if ( type == "ZeroCrossingCounter" ) {
    f = new ZeroCrossingCounter(
        p.get<int>("window"),
        p.get<double>("deadzone"),
        num_dimensions,
        p.exist("combined"));
  } else {
    f = loadFeatureExtractionFromFile(type);
    if (f==NULL) {
      cerr << c.usage() << endl << p.usage() << endl << "error: extractor " << type << "could neither be loaded as file nor does it specify one of the extraction modules" << endl;
      return f;
    }
  }

  if (p.rest().size() > 0) input_file = p.rest()[0];
  return f;
}

bool isempty(string s) {
  return s.find_first_not_of("\t\n ") == string::npos;
}

bool feature(FeatureExtraction *f, std::string line, int buffer_size)
{
  static int got_n_samples = 0;
  stringstream ss(line);
  vector<double> data;
  double value;
  string label;

  if (isempty(line) || line[0]=='#') {
    cout << line << endl;
    got_n_samples = 0;
    return true;
  }

  ss >> label;
  while (ss >> value)
    data.push_back(value);

  // XXX we handle filter lag, by removing the first n sample until the buffer
  //     is filled!
  got_n_samples++;

  bool result = f->computeFeatures(data);
  if (!result) return true;

  if (got_n_samples >= buffer_size) {
    cout << label;
    for (auto val : f->getFeatureVector())
      cout << "\t" << val;
    cout << endl;
  }

  return true;
}