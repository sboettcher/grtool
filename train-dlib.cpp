#include <iostream>
#include <stdio.h>

#include "cmdline.h"
#include "dlib_trainers.h"

using namespace std;
using namespace dlib;

trainer_template* trainer_from_args(string name, cmdline::parser &c, string &input_file);

//_______________________________________________________________________________________________________
int main(int argc, const char *argv[])
{
  /*
   *  ARGUMENT PARSING
   */

  string input_file = "-";
  cmdline::parser c;

  c.add        ("help",    'h', "print this message");
  c.add        ("verbose",    'v', "be verbose");
  c.add<int>   ("cross-validate", 'c', "perform k-fold cross validation", false, 0);
  c.add<string>("output",  'o', "store trained classifier in file", false);
  c.add<string>("trainset",'n', "split the trainig set, either no, random, or k-fold split, defaults to no split.", false, "-1");
  c.footer     ("<classifier> [input-data]...");

  /* parse common arguments */
  if (!c.parse(argc, argv, false)) {
    cerr << c.error() << endl;
    return -1;
  }
  if (c.rest().size() == 0) {
    cout << c.usage() << endl;
    cout << "Available Classifiers:" << endl;
    printTrainers();
    return -1;
  }

  string classifier_str = c.rest()[0];

  if (!classifierExists(classifier_str)) {
    cout << c.usage() << endl;
    cout << "Available Classifiers:" << endl;
    printTrainers();
    return -1;
  }

  trainer_template* trainer = trainer_from_args(classifier_str, c, input_file);

  if (c.exist("help")) {
    cout << c.usage() << endl;
    cout << "Available Classifiers:" << endl;
    printTrainers();
    return 0;
  }

  /* check if we can open the output file */
  ofstream fout(c.get<string>("output"), ios_base::out | ios_base::binary);
  ostream &output = c.exist("output") ? fout : cout;

  if (c.exist("output") && !output.good()) {
    cerr << c.usage() << endl << "unable to open \"" << c.get<string>("output") << "\" as output" << endl;
    return -1;
  }

  /* do we read from a file or stdin? */
  ifstream fin; fin.open(input_file);
  istream &in = input_file=="-" ? cin : fin;

  if (!in.good()) {
    cerr << "unable to open input file " << input_file << endl;
    return -1;
  }

  /* get all possible modes for training set selection */
  char *endptr = NULL;
  const char *file   = c.get<string>("trainset").c_str();
  double ratio = strtod(file, &endptr);
  bool isfile  = (endptr-file) - strlen(file) != 0;
  int32_t /* parse x.yy into integral and fractional part */
      integral = strtoul(file, &endptr, 10),
      fraction = *endptr=='.' ? strtoul(endptr+1, NULL, 10) : -1;

  // special case for a ratio of 100%
  if (ratio == 1) ratio = -1;

  // do some sanity checks on the arguments
  if (!isfile && ratio >= 0) {
    // k-fold specification
    if ( fraction < 0 ) {
      cerr << "no fold number given, specify with k.x or use a ratio (0,1] for random split" << endl;
      return -1;
    }

    if ( integral >= 1  && fraction >= integral ) {
        cerr << "fold number (" << fraction << ") must be less than number"
                " of folds (" << integral << ")" << endl;
        return -1;
    }

    if (ratio >= 1 && fraction < 0) {
      cerr << "either -n must be less than one to select a random split "
        "or given as k.x where k is the number of folds, and x the fold to "
        "select " << endl;
      return -1;
    }
  }

  /* per default we read from the main inputstream */
  ifstream tif; istream &tin = isfile ? tif : in;
  if (isfile) tif.open(file);



  /*
   *  READ TRAINING SAMPLES
   */

  v_sample_type train_samples;
  v_label_type train_labels;
  std::vector<int> train_indices;
  std::set<string> u_labels;

  string line, label;

  int ix = 0;
  while (getline(tin, line)) {
    stringstream ss(line);

    if (line.find_first_not_of(" \t") == string::npos) {
      if (train_samples.size() != 0)
        break;
      else
        continue;
    }

    if (line[0] == '#')
      continue;

    ss >> label;
    std::vector<double> sample;
    string val;
    while (ss >> val) // this also handles nan and infs correctly
      sample.push_back(strtod(val.c_str(), NULL));

    if (sample.size() == 0)
      continue;

    train_samples.push_back(mat(sample));
    train_labels.push_back(label);
    train_indices.push_back(ix);
    u_labels.insert(label);
    ++ix;
  }

  assert(train_samples.size() == train_labels.size());

  /* select the trainset according to given cli option */
  v_sample_type test_samples;
  v_label_type test_labels;
  std::vector<int> test_indices;

  if (isfile || ratio <= 0) {
    // ignore, no split
  } else if (ratio < 1) {
    // random split TODO: stratified
    randomize_samples(train_samples, train_labels, train_indices);
    split_array(train_samples, test_samples, ratio);
    split_array(train_labels, test_labels, ratio);
    split_array(train_indices, test_indices, ratio);
  } else if (ratio >= 1) {
    // TODO: k-fold split
  }

  assert(test_samples.size() == test_labels.size());



  /*
   *  TRAINING
   */

  // cross-validate, or train and serialize
  if (c.get<int>("cross-validate") > 0) {
    // randomize and cross-validate samples
    randomize_samples(train_samples, train_labels);
    matrix<double> cv_result = trainer->crossValidation(train_samples, train_labels, c.get<int>("cross-validate"));
    cout << classifier_str << " " << c.get<int>("cross-validate") << "-fold cross-validation:" << endl << cv_result << endl;

    cout << "number of samples: " << train_samples.size() << endl;
    cout << "number of unique labels: " << u_labels.size() << endl << endl;

    cout << "accuracy: " << trace(cv_result) / sum(cv_result) << endl;
    cout << "F1-score: " << (2 * trace(cv_result)) / (trace(cv_result) + sum(cv_result)) << endl;
  }

  else if (classifier_str == TrainerName::ONE_VS_ONE) {
      ovo_trained_function_type_rbf_df df = trainer->train(train_samples, train_labels).cast_to<ovo_trained_function_type>();
      serialize(df, output);
  }
  else if (classifier_str == TrainerName::ONE_VS_ALL) {
      ova_trained_function_type_rbf_df df = trainer->train(train_samples, train_labels).cast_to<ova_trained_function_type>();
      serialize(df, output);
  }
  else if (classifier_str == TrainerName::SVM_MULTICLASS_LINEAR) {
      svm_ml_trained_function_type df = trainer->train(train_samples, train_labels).cast_to<svm_ml_trained_function_type>();
      serialize(df, output);
  }


  if (!c.exist("output"))
    cout << endl; // mark the end of the classifier if piping
  else
    fout.close();

  if (test_samples.size() > 0) {
    for (size_t i = 0; i < test_samples.size(); ++i) {
      cout << test_labels[i];
      for (int j = 0; j < test_samples[i].size(); ++j)
        cout << "\t" << test_samples[i](j);
      cout << endl;
    }
    cout << endl;
  }
}



//_______________________________________________________________________________________________________
trainer_template* trainer_from_args(string name, cmdline::parser &c, string &input_file)
{
  trainer_template* trainer;
  cmdline::parser p;

  // add specific options
  if (name == TrainerName::ONE_VS_ONE) {
    p.add<int>("threads", 'T', "number of threads/cores to use", false, 4);
    p.add<double>("gamma", 'G', "rbf kernel gamma", false, 0.1);
  }
  else if (name == TrainerName::ONE_VS_ALL) {
    p.add<int>("threads", 'T', "number of threads/cores to use", false, 4);
    p.add<double>("gamma", 'G', "rbf kernel gamma", false, 0.1);
  }
  else if (name == TrainerName::SVM_MULTICLASS_LINEAR) {
    p.add<int>("threads", 'T', "number of threads/cores to use", false, 4);
    p.add("nonneg", 'N', "learn only nonnegative weights");
    p.add<double>("epsilon", 'E', "set error epsilon", false, 0.001);
    p.add<int>("iterations", 'I', "set maximum number of SVM optimizer iterations", false, 10000);
    p.add<int>("regularization", 'C', "SVM regularization parameter. Larger values encourage exact fitting while smaller values of C may encourage better generalization.", false, 1);
  }

  if (c.exist("help")) {
    cout << c.usage() << endl;
    cout << "specific " << name << " options:" << endl << p.str_options();
    exit(0);
  }
  if (!p.parse(c.rest())) {
    cout << c.usage() << endl;
    cout << "specific " << name << " options:" << endl << p.str_options();
    cout << p.error() << endl;
    exit(-1);
  }

  // create trainer
  if (name == TrainerName::ONE_VS_ONE)
    trainer = new ovo_trainer(c.exist("verbose"), p.get<int>("threads"), p.get<double>("gamma"));
  else if (name == TrainerName::ONE_VS_ALL)
    trainer = new ova_trainer(c.exist("verbose"), p.get<int>("threads"), p.get<double>("gamma"));
  else if (name == TrainerName::SVM_MULTICLASS_LINEAR)
    trainer = new svm_ml_trainer(c.exist("verbose"), p.get<int>("threads"), p.exist("nonneg"), p.get<double>("epsilon"), p.get<int>("iterations"), p.get<int>("regularization"));

  else {
    cout << "wtf" << endl;
    exit(-1);
  }

  if (p.rest().size() > 0)
    input_file = p.rest()[0];

  return trainer;
}
