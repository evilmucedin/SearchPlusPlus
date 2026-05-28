// Python (pybind11) bindings for SearchPlusPlus.
//
// Naming convention: Python-side methods are snake_case (idiomatic Python),
// while the underlying C++ API is PascalCase / camelCase. The mapping is
// done in lambdas below so the C++ API stays untouched.
//
// Error handling: every C++ call that returns Expected<T> or Status is
// wrapped in a small helper that throws a Python exception on failure —
// callers get normal `raise` semantics instead of having to ask `.ok()`.

#include "spp/spp.h"

#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include <memory>
#include <string>
#include <string_view>

namespace py = pybind11;

namespace {

// Map StatusCode → a Python exception class. We pick existing built-ins
// where the meaning lines up; anything we don't have a clear mapping for
// becomes RuntimeError so callers can still `except RuntimeError:`.
void RaiseFromStatus(const spp::Status& s) {
    if (s.ok())
        return;
    const std::string msg = s.ToString();
    switch (s.code()) {
        case spp::StatusCode::kInvalidArgument:
            throw py::value_error(msg);
        case spp::StatusCode::kNotFound:
            throw py::key_error(msg);
        case spp::StatusCode::kAlreadyExists:
            throw py::value_error(msg);
        case spp::StatusCode::kOutOfRange:
            throw py::index_error(msg);
        case spp::StatusCode::kUnimplemented:
            throw std::runtime_error("NotImplemented: " + msg);
        case spp::StatusCode::kOk:
        case spp::StatusCode::kFailedPrecondition:
        case spp::StatusCode::kCorruption:
        case spp::StatusCode::kIoError:
        case spp::StatusCode::kInternal:
        case spp::StatusCode::kAborted:
        case spp::StatusCode::kUnavailable:
            throw std::runtime_error(msg);
    }
}

template <class T>
T Unwrap(spp::Expected<T> e) {
    if (!e.ok())
        RaiseFromStatus(e.status());
    return std::move(*e);
}

}  // namespace

PYBIND11_MODULE(searchplusplus, m) {
    m.doc() =
        "Python bindings for SearchPlusPlus. Mirrors the C++ API in spp::index "
        "and spp::query with snake_case method names and Python exceptions in "
        "place of Expected<T>/Status.";

    py::class_<spp::index::Schema>(m, "Schema")
        .def(py::init<>())
        .def(
            "add_text_fields",
            [](spp::index::Schema& self, const std::vector<std::string>& names) {
                // Schema::AddTextFields takes std::initializer_list — which
                // can't be constructed from a runtime range — so replicate
                // its behavior by adding one text field at a time.
                for (const auto& name : names) {
                    spp::index::FieldMapping m;
                    m.name = name;
                    m.type = spp::index::FieldType::kText;
                    RaiseFromStatus(self.AddField(std::move(m)));
                }
            },
            py::arg("names"),
            "Add several text fields with the default analyzer. Raises on the "
            "first duplicate or empty name.")
        .def(
            "has_field",
            [](const spp::index::Schema& self, std::string_view name) {
                return self.HasField(name);
            },
            py::arg("name"))
        .def("field_count",
             [](const spp::index::Schema& self) { return self.field_count(); });

    py::class_<spp::index::Document>(m, "Document")
        .def(py::init<>())
        .def_readwrite("id", &spp::index::Document::id)
        .def_readwrite("fields", &spp::index::Document::fields);

    // IndexReader is exposed as an opaque shared_ptr — the Python side only
    // needs to pass it to Searcher; it has no methods that callers consume.
    // pybind11 doesn't accept `shared_ptr<const T>` as a holder, so we hold
    // the non-const version and const_pointer_cast at the C++ boundary; this
    // is purely a marshalling trick — no mutating methods are exposed to
    // Python so the object stays effectively read-only.
    py::class_<spp::index::IndexReader, std::shared_ptr<spp::index::IndexReader>>(
        m, "IndexReader");

    // IndexWriter is non-copyable; pybind11 needs a holder that can own a
    // unique_ptr. We give it the std::unique_ptr holder so transferring
    // ownership from the C++ factory works without an extra Move.
    py::class_<spp::index::IndexWriter, std::unique_ptr<spp::index::IndexWriter>>(
        m, "IndexWriter")
        .def_static(
            "open",
            [](const std::string& dir_path, const spp::index::Schema* initial_schema) {
                spp::index::IndexOpenOptions opts;
                opts.initial_schema = initial_schema;
                return Unwrap(spp::index::IndexWriter::Open(dir_path, opts));
            },
            py::arg("dir_path"),
            py::arg("initial_schema") = nullptr,
            "Open an index writer at `dir_path`. If the directory is empty, "
            "`initial_schema` is used to bootstrap; otherwise the on-disk "
            "schema wins and `initial_schema` is ignored.")
        .def(
            "add_document",
            [](spp::index::IndexWriter& self, const spp::index::Document& doc) {
                RaiseFromStatus(self.AddDocument(doc));
            },
            py::arg("doc"))
        .def(
            "refresh",
            [](spp::index::IndexWriter& self) {
                return Unwrap(self.Refresh());
            },
            "Seal the in-memory segment and publish a new reader. Returns the "
            "new generation.")
        .def(
            "close",
            [](spp::index::IndexWriter& self) { RaiseFromStatus(self.Close()); })
        .def(
            "current_reader",
            [](const spp::index::IndexWriter& self) {
                return std::const_pointer_cast<spp::index::IndexReader>(self.CurrentReader());
            },
            "Latest published reader. Lock-free; safe to call from any "
            "thread.");

    py::class_<spp::query::Hit>(m, "Hit")
        .def_readonly("id", &spp::query::Hit::id)
        .def_readonly("score", &spp::query::Hit::score);

    py::class_<spp::query::SearchResult>(m, "SearchResult")
        .def_readonly("total_hits", &spp::query::SearchResult::total_hits)
        .def_readonly("hits", &spp::query::SearchResult::hits);

    py::class_<spp::query::Searcher>(m, "Searcher")
        .def(py::init([](std::shared_ptr<spp::index::IndexReader> reader) {
                 return spp::query::Searcher(
                     std::const_pointer_cast<const spp::index::IndexReader>(std::move(reader)));
             }),
             py::arg("reader"))
        .def(
            "search",
            [](spp::query::Searcher& self,
               const std::string& query,
               const std::string& default_field,
               std::size_t size) {
                spp::query::SearchOptions opts;
                opts.default_field = default_field;
                opts.size = size;
                return Unwrap(self.Search(query, opts));
            },
            py::arg("query"),
            py::arg("default_field") = std::string{},
            py::arg("size") = 10);
}
