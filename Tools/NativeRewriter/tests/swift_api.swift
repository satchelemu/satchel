import NativeRewriter

func rewrite(_ request: inout nr_request) -> nr_status {
    var result: OpaquePointer?
    var error = nr_error()
    let status = nr_rewrite(&request, &result, &error)
    if let result {
        let image = nr_result_get(result, NR_IMAGE)
        _ = image.size
        nr_result_free(result)
    }
    return status
}
