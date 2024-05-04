#include <map>
#include <string>

using namespace std;

struct TDocumentMain
{
    string maxText;
    uint64_t maxFetchTime;
    uint64_t minPubDate;
    uint64_t minFirstFetchTime;
};

struct TDocument
{
    string URL;
    uint64_t PubDate;
    uint64_t FetchTime;
    string Text;
    uint64_t FirstFetchTime {};
};

class  TProcessor
{
public:
    TProcessor() = default;
    ~TProcessor() = default;

    map<string, TDocumentMain> t_documents {};

    void updateTDocumentMain(const TDocument & doc);
    void getNewDocument(TDocument & doc);
};

