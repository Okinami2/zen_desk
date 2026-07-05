#include <stdio.h>
#include "svp_acl.h"
#include "svp_acl_mdl.h"

int main() {
    svp_acl_init(NULL);
    svp_acl_rt_set_device(0);
    uint32_t model_id;
    svp_acl_mdl_desc *desc;
    svp_acl_mdl_load_from_file("/home/hispark/pegasus/platform/ss928v100_gcc/smp/a55_linux/mpp/zen_desk/data/model/pose_detector.om", &model_id);
    desc = svp_acl_mdl_create_desc();
    svp_acl_mdl_get_desc(desc, model_id);
    size_t in_num = svp_acl_mdl_get_num_inputs(desc);
    size_t out_num = svp_acl_mdl_get_num_outputs(desc);
    printf("Inputs: %zu, Outputs: %zu\n", in_num, out_num);
    for (size_t i = 0; i < out_num; i++) {
        printf("Output %zu: size=%zu\n", i, svp_acl_mdl_get_output_size_by_index(desc, i));
    }
    return 0;
}
