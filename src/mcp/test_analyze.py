"""qlens_analyze 集成测试：真实图片 → QC 检测 → 写 qltag.db → 验证。"""
import os
import shutil
import tempfile

import qc_detect
import qlens_lib


def main():
    src = r"E:\Users\NDark\Pictures\0035.jpg"  # 已知有色偏/曝光过度
    if not os.path.exists(src):
        print("SKIP: no test image")
        return

    test_dir = tempfile.mkdtemp(prefix="qlens_qc_test_")
    img = os.path.join(test_dir, "sample.jpg")
    shutil.copy2(src, img)

    # 1. 直接检测
    qc = qc_detect.detect_qc(img)
    print("detect:", qc)
    assert qc, "QC 检测应返回结果"

    # 2. 通过 analyze 写库
    results = qc_detect.analyze_folder(test_dir)
    assert results, "analyze_folder 应返回结果"
    for item in results:
        for tag, conf in item["qc"].items():
            if conf >= 0.5:
                qlens_lib.add_tag_with_confidence(item["path"], tag, conf)

    # 3. 验证标签已写入
    tags = qlens_lib.get_tags(img)
    print("written tags:", tags)
    assert tags, "标签应写入 qltag.db"

    # 4. 清理
    qlens_lib.close_all()
    shutil.rmtree(test_dir, ignore_errors=True)
    print("ANALYZE INTEGRATION OK")


if __name__ == "__main__":
    main()
