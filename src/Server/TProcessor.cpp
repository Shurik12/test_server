#include <Server/TProcessor.h>

void TProcessor::updateTDocumentMain(const TDocument & doc)
{
    if (auto search = t_documents.find(doc.URL); search != t_documents.end())
    {
        auto main_doc = search->second;
        if (main_doc.maxFetchTime < doc.FetchTime)
        {
            main_doc.maxFetchTime = doc.FetchTime;
            main_doc.maxText = doc.Text;
        }

        if (main_doc.minFirstFetchTime > doc.FetchTime)
        {
            main_doc.minPubDate = doc.PubDate;
            main_doc.minFirstFetchTime = doc.FetchTime;
        }
    }
    else
    {
        TDocumentMain main_doc {
            doc.Text,
            doc.FetchTime,
            doc.PubDate,
            doc.FetchTime};

        t_documents[doc.URL] = main_doc;
    }
}

void TProcessor::getNewDocument(TDocument & doc)
{
    auto search = t_documents.find(doc.URL);
    auto main_doc = search->second;
    doc.PubDate = main_doc.minPubDate;
    doc.Text = main_doc.maxText;
    doc.FirstFetchTime = main_doc.minFirstFetchTime;
}